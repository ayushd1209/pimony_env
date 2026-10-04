# Analysis pass over the pim.gemv result — gaps found, and what they were worth

Opened 2026-09-16 while trying to make the 5.70x result defensible. Everything
here sits in the **software and harness around** the instruction.
**The sequencer validation is untouched** — 110,592 / 32 / 8,448 / 8 were
predicted and matched exactly, and nothing below changes that.

⚠️ **2026-09-18, one of those four is now WRONG.** `num_readres_cmds` = 8,448 was
our defect, not a measurement: the sequencer issued one READRES per *bank*, and
READRES has no bank field. Correct count is **2,112**, one per bankgroup. The
prediction matched because it checked our arithmetic against itself. See
G7 at the end of this file. The other three (110,592 / 32 / 8) stand.

Status: `CLOSED` · `OPEN`

## THE NUMBERS — 2026-10-02 · after the O3 fence fix (G13) · QUOTE FROM HERE

**Every O3 PIM figure dated before 2026-10-02 in this file is superseded.** On O3,
`pim.fence` released its barrier when it executed, while its clean/invalidate left the
store queue only after commit: later loads, and the next fence, ran before the cache
operation. Fixed in `ed427c0`; see G13 at the end. Fences now serialize on O3 as they
always did on Minor, so O3's PIM runs got slower. TimingSimple, Minor and every
baseline are unchanged (no `pim.fence` in the baselines; Minor's model was correct).

Headline host pair, **decided by Ayush 2026-10-02: default O3 vs default Minor.** The two
cores also differ in vector hardware (O3: 4 SIMD units, opLat 1; Minor: 1 unit, opLat 6),
so the gap is not only in-order vs out-of-order. O3 with Minor's vector unit (`6 1`) is
kept as a secondary row.

```
matmul blocks 1/3/5/7, cycles = simTicks/333.33     baseline         PIM     speedup   (before fix)
BERT   Minor                                       6,189,410     374,130     16.54x    16.54x
BERT   O3 default                                  3,194,329     253,794     12.59x    15.60x
BERT   O3, Minor's vector unit                     3,623,447     256,515     14.13x    17.53x
BERT   O3, Minor's vector unit, a16 / a32 layout   3,623,447     251,371 / 247,469   14.41x / 14.64x   (18.12x / 18.55x)
GPT-3  Minor                                      64,814,111   2,290,760     28.29x    28.29x
GPT-3  O3 default                                 25,823,931   1,589,782     16.24x    19.51x
GPT-3  O3, Minor's vector unit                    30,558,375   1,612,483     18.95x    22.81x
```

⇒ **Minor shows the larger speedup on both workloads.** "Out-of-order adds 6–12%" (G12)
came from the fence bug and is **withdrawn**. In absolute time O3 + PIM is still the
fastest machine on both. Fixed vs old O3 PIM runs: instructions identical, PIM sleep
−0.5% to +3.2%, only the host-side fences got dearer (+24% BERT, +20% GPT-3 PIM time).

Reproduce (paths as on 2026-10-02; divide baseline by PIM):

```
cd gem5/m5out && for d in bert_blas_fair_minor bert_pim_g7pt_minor bert_blas_fair_o3_fix bert_pim_g7pt_o3_f2 bert_blas_fair_o3v61 bert_pim_g7pt_o3v61_f2 gpt3_blas_c_minor gpt3_pt_minor gpt3_blas_c_o3 gpt3_pt_o3_f2 gpt3_blas_c_o3v61 gpt3_pt_o3v61_f2; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^simTicks /{t[b]=$2} END{printf "%-24s %d\n", d, (t[1]+t[3]+t[5]+t[7])/333.33}' $d/stats.txt; done
```

**G8.1 re-measured 2026-10-03:** fences cost 22.7% of BERT PIM matmul time on the fixed O3
(was 5.0%) — the "not worth building" verdict is withdrawn; see G8.1.

**Rerun on the fixed O3, 2026-10-03 — every O3 result in this file and in
`gpt3/GAPS_gpt3.md` is now on the fixed build:**

- **G9, `pim.gemv` vs `pim.dispatch`: 1.30x, not 13.9x.** "32 dispatches, then wait"
  RUNS — the old "hangs at 2,109" was our own 64-token bug — so it is the schedule to
  compare against. 1 at a time = 11.45x; all 192 at once still crashes PIMony. See G9.
- **Overlap** (`gpt3/GAPS_gpt3.md`): block-1 sleep filled **45,054** cycles (was 21,369);
  **79%** of the sleep is unreachable for one inference stream (was 77%).
- **Interference** (G-INT): host work in the PIM wait costs **+5.1% per L2 miss** against a
  host that only sleeps (CPU first; was 6.54%) and +8.7% with PIM first (was 8.46%); the
  attention step alone +3.2% (was 4.69%). PIM first slows the PIM's own jobs +19.8%;
  the write-back suspect is ruled out, cause open.

```
cd gem5/m5out && for d in bert_disp_o3_f2 bert_disp_w32_o3_f2 bert_pim_g7_o3_f2; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^simTicks /{t[b]=$2} END{printf "%-22s %d\n", d, (t[1]+t[3]+t[5]+t[7])/333.33}' $d/stats.txt; done
```
Overlap and interference one-liners: top block of `gpt3/GAPS_gpt3.md`.

## THE NUMBERS — 2026-09-30 · in-order vs out-of-order (see G12)

⚠️ **SUPERSEDED 2026-10-02 for every O3 row** (fence bug, see the block above and G13).
The Minor and TimingSimple rows stand.

**Quote the host comparison from here.** Same binary pair on every pipelined host
(`bert_pim_g7pt` / `bert_blas_fair`), matmul blocks 1/3/5/7, cycles = `simTicks`/333.33.

```
                               baseline        PIM     matmul   layer*
TimingSimple  (first pass)    7,370,634    874,066     8.43x    7.21x   <- g7; mention briefly
Minor         (in-order)      6,189,410    374,130    16.54x   12.87x
O3            (out-of-order)  3,623,447    206,714    17.53x   14.46x   <- Minor's vector unit
O3, gem5 default FUs          3,194,329    204,733    15.60x   14.20x
```
\* whole layer is unsound on every pipelined host (PIMony computes no values); quote matmul.

**PIM's advantage roughly doubles once the host is a pipelined core; out-of-order
adds a further 6–12%.** The range is code layout: across three builds Minor moves
±0.3%, O3 moves 6% (6 / 9 / 12%). "O3" in the comparison runs with **Minor's vector
unit** (1 unit, opLat 6) so the two cores differ in issue order, not vector width.
The 6–12% is an upper bound — Minor's single FP/SIMD unit also slows PIM's scalar FP,
which the matched O3 kept fast.

⇒ **G8.3's mechanism was wrong as stated.** The widening is already there on an
in-order core (Minor helps PIM 2.34x, the baseline 1.19x). It comes from the baseline
being memory-bound, not from out-of-order overlap.

**GPT-3 decode, same method (`gpt3_pt` / `gpt3_blas_c`) — THE ORDER FLIPS:**

```
                               baseline        PIM     matmul
Minor         (in-order)     64,814,111  2,290,760    28.29x
O3            (out-of-order) 30,558,375  1,339,410    22.81x   <- Minor's vector unit
O3, gem5 default FUs         25,823,931  1,323,866    19.51x
```

⇒ **"Out-of-order adds 6–12%" is a BERT result, not a rule.** On GPT-3 the in-order
host shows the bigger ratio, by 24%. Out-of-order speeds the streaming baseline up
2.12x (6.8 vs 1.5 L2 misses in flight) but the PIM run only 1.71x, because 72% of
that run is the host asleep while the device works. Record and checks:
`gpt3/GAPS_gpt3.md` top block. Quote the host comparison per workload, never as one
number. In absolute time O3 + PIM is fastest on both.

## THE NUMBERS — 2026-09-19

**THE SPEEDUP IS A PROPERTY OF THE HOST, NOT OF THE INSTRUCTION.** Quote it with
the CPU model attached, always.

**The other headline, G9:** against `pim.dispatch` doing the same matmuls on the
same host — **13.9x** on the matmul sections, and `pim.dispatch` does *less*
(no buffer fill, no readout). Two of the three schedules it can be given do not
run at all. ⚠️ **Superseded 2026-10-03: 1.30x — the 32-at-a-time schedule runs (G9).**

```
                            in-order host    out-of-order host
                            (TimingSimple)        (O3)
matmul only (blk 1,3,5,7)       8.24x            15.28x   <- THE HEADLINE
whole layer                     7.46x            14.72x
```

⚠️ **CORRECTION 2026-09-24 — the WHOLE-LAYER figures above are inflated. The
matmul figures are not.** `bert_blas.c` used libm `expf`/`tanhf` while `bert.c`
used its own `k_expf`/`k_tanhf`. Those run on the CPU in **both** builds, so the
difference was pure measurement error — block 6 (GELU) came out **19.39x** apart
on o3 for identical work. Re-measured with `bert_blas_fair.c`, which is
`bert_blas.c` with those two calls swapped and nothing else:

```
                            TimingSimple           O3
matmul only          8.24x -> 8.43x      15.28x -> 15.30x     <- essentially unmoved
whole layer          7.46x -> 7.21x      14.72x -> 13.95x     <- these were wrong
```

**Quote 15.30x / 8.43x, and 13.95x / 7.21x.** The matmul figures moving by
+0.13% and +2.3% (inside the ±3% floor) is the point: blocks 1/3/5/7 never
contained these calls, so the headline was never exposed. This is precisely what
the "quote matmul-only" rule was protecting against.

Runs: `m5out/bert_blas_fair_{o3,timing}`. PIM side unchanged, `bert_pim_g7_*`
stands. `bert_blas.c` and `blas_s1_fp16_phase*` untouched.

⚠️ **The residual gap is NOT fixable.** After the swap, o3 block 6 is still
2.63x and block 2 is 1.76x, because **PIMony computes no values** — the PIM
build's activations are essentially the biases, so `k_expf`/`k_tanhf` take
different early-out branches than the baseline's real data. Measured at 18.1
instructions per `exp` call. On TimingSimpleCPU the same block 6 is **0.97x**,
because that CPU blocks on every access and hides the IPC difference.
⇒ **whole-layer comparison is unsound on any out-of-order host.** Only blocks
1/3/5/7 are branch-free and comparable. Full record in
`gpt3/GAPS_gpt3.md`.

⚠️ **ALWAYS DERIVE THESE FROM `simTicks`, NEVER FROM `numCycles`.** On O3
`numCycles` counts only ACTIVE cycles, so the time the core sleeps in `pim.wait`
vanishes — and PIM sleeps for 41.8% of the layer while the baseline never sleeps.
Using `numCycles` inflated this to 28.51x on 2026-09-19 before the check caught
it. Cycles below are `simTicks / 333.33` at 3 GHz.

| | matmul | layer |
|---|---|---|
| OpenBLAS, timing | 7,198,834 | 7,836,692 |
| PIM, timing | 874,066 | 1,050,647 |
| OpenBLAS, **o3** | 3,189,369 | 3,416,506 |
| PIM, **o3** | 208,743 | 232,162 |

A better host **widens** PIM's advantage ~2x, because O3 helps PIM 4.5x and the
memory-bound baseline only 2.3x (IPC 1.93 vs 0.43). See G8.3. Also measured
there: 21.8x OpenBLAS-vs-hand-written, and the SE/FS mode tax on O3 = 0.00027%.

**These replace 11.3x / 9.55x**, which were measured while the host was throwing
PIM's results away. The drop is the workload becoming complete, not the design
getting worse — see G6 step 5 at the end of this file. Same baseline run
(`blas_s1_fp16_phase`), so only PIM's side moved. Treat as ±3%: `f/i` shifted in
blocks 3 and 5.

⚠️ The 246x hand-written-kernel figure is not recomputed and should not be
quoted; its self-consistency check (11.3 x 21.8 = 246) belonged to the old
numbers and no longer closes.

**Quote the matmul-only number.** It is free of G3 by construction (the differing
code is not in those sections) and it is empirically more stable: across code
layouts it moved 3.6%, the whole-layer number moved 11%.

**SELF-CONSISTENCY CHECK (historical, 2026-09-16 numbers):** 11.3 x 21.8 = 246,
closing to three digits. Three independently measured quantities that had to
agree, and did — that validated sectioning, machine-identity and the layout
sweep, and it still does for the chain. It does **not** carry over to 8.24x: the
hand-written side was never re-run against the completed result path, so the
triangle has one stale corner. Re-run `bert_cpu` if you want it to close again.

Matmul-block cycles: hand-written CPU **156,979,826** / OpenBLAS **7,205,968** /
PIM **637,977**.

### How it moved

```
 5.70x   as published in RESULTS_pim_gemv.md
11.42x   G1 closed  (zfh: FP16 conversion in hardware, not libgcc)
10.64x   G2 closed  (operand vector actually flushed to DRAM)
         ...but measured on a build that got LUCKY in block 7
 9.55x   G5 closed  (layout sampled; the lucky build was the outlier)
 8.24x   G6 step 5  (the host actually reads the results back)
```

### The layout sweep — `-falign-functions` default / 16 / 32

Same source, same flags, only code position differs. **Run this on both sides of
any A/B.** It mattered all three times we used it:

| | default | a16 | a32 | verdict |
|---|---|---|---|---|
| **PIM** blk7 | 289,332 (fetch 1.084) | 355,212 (1.450) | 354,918 (1.450) | default was **lucky**, +23% |
| **OpenBLAS** blk7 | 2,045,875 (1.402) | 2,049,513 (1.408) | 2,048,843 (1.408) | **stable, 0.18%** |
| **hand-written** ROI | 171,249,822 (1.571) | 157,162,510 (1.143) | 157,165,021 (1.143) | default was **unlucky**, +9% |

Rule: **two independent alignments agreeing is the level; a lone outlier is luck.**
OpenBLAS's 1.402 is intrinsic to `shgemv`, not a fluke — it was the only one of
the three that did not move.

Determinism re-confirmed: `bert_pim_flush` rerun reproduced 737,116 cycles exactly.

Runs: `m5out/bert_pim_zfh` (G1), `m5out/bert_pim_flush` (G1+G2),
`m5out/blas_s1_fp16_phase` (baseline, 7,844,460 cycles). All `-DPHASE_STATS`,
8 sections per `bert_layer()`, blocks 1/3/5/7 = matmuls, 2/4/6/8 = control.

---

## G1 — FP16 conversion was emulated in software · CLOSED 2026-09-16

`bert.c:351` in `pim_store_vec`, and `bert_blas.c:183` in `linear()`:

```c
wt_t e = (wt_t)v[i];      /* float -> _Float16 */
```

Both binaries were built **without `zfh`**, so this cast compiled to
`jal __truncsfhf2` — a 174-instruction libgcc soft-float routine. PIM ran it
5,376x per layer, the baseline 6,912x, all inside the ROI.

**Both sides paid it, so the comparison was even-handed — but it landed ~4x
harder on PIM**, because `pim_store_vec` is most of PIM's remaining work while
the baseline's conversions are a slice of a much bigger total. Removing an
even-handed cost from both sides therefore moved the ratio a lot.

Measured rate, from the baseline rebuild: **56.7 instructions and 157 cycles per
conversion** (1,872,593 -> 1,480,400 insts; 8,932,476 -> 7,844,460 cycles).

- **Fix:** `-march=rv64gcv_zfh`. gem5 implements scalar zfh (`flh`, `fsh`,
  `fcvt_s_h`, `fcvt_h_s`, `fadd_h`…) — checked in `decoder.isa`.
- **Result:** PIM ROI 1,563,632 -> 686,772 cycles, 495,969 -> 189,414 insts.
  Predicted 191,000 insts before the run; came in at 189,414 (-0.8%).
- **Rule that falls out:** the host ISA must match the workload's precision. A
  host without FP16 is not a conservative choice, it is the **wrong machine** —
  you end up measuring libgcc.

## G2 — nothing cleaned the operand vector out of the cache · CLOSED 2026-09-16

`pim_store_vec` writes `pimvec[]` with ordinary stores, so the vector sat dirty
in cache. `D2GWRITE` reads DRAM. **The PIM was computing on stale bytes.**
Invisible here only because PIMony moves no data and computes nothing.

The single `pim_fence_cl` in the file covered the **descriptor**, not the operand.

**Fix (option 1 of 3, see below):** `pim_flush_vec()` in `bert.c`, called after
each `PIM_STORE_VEC`. 2 lines per 512 B stride (128 useful bytes = 4 channel
copies x 32 B); 96+96+96+384 = **672 lines per layer**.

**Measured cost: +50,348 cycles = 74.9 cycles per line.** Estimated 57.

- **They do NOT pipeline.** I expected back-to-back flushes to different lines to
  overlap and come in under 38,000. They didn't; each pays full latency.
  ⇒ **This is the measured case for a range fence.** 7% of runtime spent
  serialising an operation that is semantically "clean one region".
- **Scaling check passed:** block 7 (384 lines) absorbed 4.22x block 3 (96 lines).
- **Unexpected: idle DROPPED 90,367 -> 86,980 (-3.7%).** The PIM does identical
  work, so this is interference. Those 672 dirty lines used to be evicted to DRAM
  *while the PIM was running*, preempting MACs. Flushing up front removes the
  collision. **Part of the fence's cost pays for itself.**

**The three options, and why option 1 first:**

| | | cost |
|---|---|---|
| 1. 672 x `pim.fence.cl` | uses the instruction as built | **measured 50,348 cyc** |
| 2. range fence | one instruction, base+length | unbuilt ISA; now justified by the 74.9 cyc/line above |
| 3. `pimvec` uncacheable | nothing to flush | untested; cost moves *into* `pim_store_vec` |

Option 3 is still worth one run — it is the A/B for **"when is a fence better
than an uncacheable region?"**, which is a real ISA design question. Note it
would remove `fence.cl` from the operand path but **not** from the descriptor,
the weights, or — the strong one — the **result** path, where `pim.fence.inv` is
needed because the CPU reads PIM output repeatedly (GELU, LayerNorm, residual).

Framing to keep: *a fence pays off when the CPU works with data in cache and
hands it over once; an uncacheable region wins for write-once, hand-off-
immediately buffers. The operand vector is the second kind, the result vector is
the first.*

## G3 — baseline and PIM are different source files · AVOIDED (not fixed)

`RESULTS_pim_gemv.md` claims "one variable: how the six matmuls are done".
Not accurate — two files, and three of four CPU-side kernels differ:

| kernel | `bert.c` | `bert_blas.c` | real? |
|---|---|---|---|
| `residual_add` | — | — | identical (md5) |
| `layernorm` | `k_sqrtf` | `__builtin_sqrtf` | **no** — `bert.c:112` aliases them |
| `gelu` | `k_tanhf` hand-rolled | libm `tanhf` | **yes**, 3072 calls/layer, 4.1x cost |
| `attention` | hand-written loops | **24 x `cblas_sgemm`** | **yes**, 2.7x cost |

**Measured:** control blocks (2,4,6,8) total 638,490 on the baseline vs 119,573
on PIM ⇒ **~519,000 cycles of contamination**, worth about **0.75x** of the
ratio. It was ~6% when PIM was slower; it matters more now that PIM is 737k.

**Why the fork exists** (`project_fs_linux_baseline_fork`, 2026-08-25): forced.
OpenBLAS needs libc; libc needs SE mode or Linux; Linux owns `mtvec`/`stvec` so
the bare-metal PIM trap handler has nowhere to live. Option B (FS+Linux + a PIM
driver) was costed and rejected.

**What was calibrated:** the SE-vs-FS **mode** tax, 0.36%, on *identical code*.
That settles the mode, **not** the code. It was carried into
`RESULTS_pim_gemv.md` as if it covered both.

## G4 — the two-tier comparison · CLOSED 2026-09-16

The 2026-08-25 decision (option A) was:

> keep PIM bare-metal, use **a hand-written baseline for the PIM comparison
> specifically**, and **cite the OpenBLAS numbers to bound** how far that
> hand-written kernel is from optimal.

```
tier 1 (the comparison):  PIM  vs  hand-written CPU   <- SAME FILE (bert.c), true ditto
tier 2 (the bound):       hand-written CPU  vs  OpenBLAS
```

`RESULTS_pim_gemv.md` fused them and compared PIM straight to OpenBLAS across the
fork. **That is how G3 got in.**

Tier 1 costs one recompile — drop `-DPIM_GEMV` and `LINEAR()` expands to the
hand-written `linear()`. `bert.c:196` already provides `-DPIM_FP16` for exactly
this ("gives an FP16 CPU baseline, so the host comparison can be made against the
same precision"). **It has never been used.** Build it with `zfh` (see G1).

Tier 1 will show a *larger* speedup than the OpenBLAS comparison, because the
hand-written kernel is worse. That is the point of tier 2. **Report both.**

## G5 — instruction-fetch alignment perturbs any A/B · CLOSED 2026-09-16 (sampled)

Found chasing a 1.8x swing in the GELU control block between two PIM runs whose
only difference was `-march`. Instructions, micro-ops, instruction-type
histogram, cache accesses/hits/misses and prefetcher stats were all **identical**.

`system.cpu.icache.demandAccesses` was not: **64,986 vs 34,229** for the same
33,457 instructions.

**Mechanism, verified in gem5 source** (`arch/riscv/decoder.cc`,
`Decoder::moreBytes`): a 32-bit instruction at an address that is not a multiple
of 4 sets `instDone = false`, so `decode()` returns null and
`cpu/simple/base.cc:388` does `fetchOffset += moreBytesSize()` and **fetches
again**. Compressed instructions at an odd position still finish in one fetch.
TimingSimpleCPU blocks on every fetch, so this lands straight in the cycle count.

Confirmed statically: in the GELU loop, **92% of 32-bit instructions misaligned
in the old build, 15% in the new**. Predicted fetch rate 1.92 vs measured 1.943.

**The guard — run on both sides of every A/B:**

```sh
awk '/Begin Simulation Statistics/{b++} \
     /^system\.cpu\.icache\.demandAccesses::total/{a[b]=$2} \
     /^system\.cpu\.commitStats0\.numInsts /{n[b]=$2} \
     END{for(k=1;k<=b;k++) if(n[k]+0) printf "blk %-3s %8.3f\n",k,a[k]/n[k]}' \
  m5out/DIR/stats.txt
```

`ratio = 1 + (fraction of executed instructions that are misaligned 32-bit)`.
~1.0 clean, ~2.0 every instruction fetched twice.
**NOTE: the 1.0 target is SimpleCPU-specific.** O3/Minor fetch whole blocks, so
their ratio sits well below 1 — there, compare the two runs to each other.

**`-falign-loops=4` does NOT fix it** (tested: misalignment went 36% -> 45%). It
aligns the loop *head*; one compressed instruction in the body flips alignment
for everything after it. The only guaranteed fix is dropping the `c` extension
(tested: 0 compressed, 0 misaligned) — but that makes code ~20% bigger and
changes the machine you are modelling. **So: detect, don't prevent.**

Live example — the guard caught a new shift immediately after the G2 edit:
block 2 (attention) went 1.322 -> 1.793 and its cycles rose 11%, from code layout
alone. **And it is currently biasing block 7**, the biggest matmul: PIM 1.084 vs
BLAS 1.402.

Not a gem5 quirk — cite Mytkowicz et al., *"Producing Wrong Data Without Doing
Anything Obviously Wrong!"*, ASPLOS 2009 (link order and environment size
perturbing measured speedups enough to flip conclusions).

---

## Current per-section numbers (`m5out/bert_pim_flush` vs `blas_s1_fp16_phase`)

| blk | section | PIM busy | PIM idle | BLAS | PIM fetch | BLAS fetch |
|---|---|---|---|---|---|---|
| 1 | QKV | 96,315 | 23,133 | 2,438,525 | 1.441 | 1.422 |
| 2 | attention | 18,538 | – | 41,631 | **1.793** | 1.281 |
| 3 | Wo | 77,949 | 7,486 | 545,167 | 1.450 | 1.425 |
| 4 | resid+LN | 14,949 | – | 17,232 | 1.272 | 1.411 |
| 5 | W1 | 92,578 | 29,266 | 2,176,401 | 1.445 | 1.424 |
| 6 | GELU | 74,758 | – | 563,373 | 1.023 | 1.017 |
| 7 | W2 | 262,237 | 27,096 | 2,045,875 | **1.084** | **1.402** |
| 8 | resid+LN | 12,813 | – | 16,254 | 1.227 | 1.411 |

ROI: **737,120** cycles / 192,879 insts. Idle (= PIM compute) **86,980 = 11.8%**.
Amdahl ceiling with an infinitely fast PIM: **12.07x**. Current: 10.64x.

## G6 — `pim.gemv` HAS NO OUTPUT ADDRESS · **MOSTLY CLOSED 2026-09-18**

> **RESULT: the result path costs 0.77% of a BERT layer.** Two runs identical in
> instruction count (192,886) *and* fetch/inst (1.492), so the difference is the
> writes and nothing else: **849,901 → 856,445 cycles, +6,544**. 2,112 writes, one
> per MAC command, 8,448 results, 16,896 B, ~3 cycles each. `quiesce = 8`.
>
> **The asymmetry is the finding.** Operand delivery (`pim_store_vec`) is **80% of
> CPU busy time**; result write-back is **0.77%**. Getting data *into* PIM costs two
> orders of magnitude more than getting results *out*. The second number did not
> exist before this work.
>
> **Done:** the descriptor's third field `out_base`, and the sequencer turning each
> drained readout into a write. **Left:** bert.c actually *consuming* the results —
> W2's three partial sums, `pim.fence.inv` on the output range, and the FP16-vs-float
> type contract. See the section at the end of this file.

The original statement of the gap follows.

```c
struct pim_gemv_desc { uint64_t w_base; uint64_t v_base; };   /* bert.c:370 */
```

Two fields. **The instruction never says where results go.** `pim_linear` ends
with `y[o] = b[o]` because that is all it *can* do.

This is not a modelling shortcut — it is an unfinished half of the ISA, and all
three previously-filed "known limits" are the same limit wearing different hats:

| blocked | why |
|---|---|
| summing W2's 3 partial sums | no address to read them from |
| the cost of consuming results | nothing lands in memory |
| `pim.fence.inv` on the return path | nothing to invalidate |

**READRES is half-modelled:** the commands are issued, timed and drained inside
the memory (8,448 issued = 8,448 done, predicted then matched — but see G7: the
right count is 2,112; 8,448 was a 4x over-issue of ours). What is absent is
everything *after* the drain — no destination write, no DRAM write traffic, no
coherence cost, nothing for the CPU to read back.

**What closing it takes:** a third descriptor field (`out_base`) plus READRES
generating write traffic to it in `memory_system.cc`. Real values are not needed —
PIMony computes nothing and never will — but the **writes** are, because that is
where the traffic and the coherence cost live.

Ayush 2026-09-16: "this is a big change and deserves a new thread." Agreed — it is
an ISA change plus sequencer work, i.e. his contribution, not glue. The operand
path is now measured down to 17.6 cyc/store and 74.9 cyc/line; the result path is
entirely unmeasured.

---

## Status

| | | |
|---|---|---|
| G1 | FP16 emulated in libgcc | **closed** — `zfh` |
| G2 | operand vector never flushed | **closed** — `pim_flush_vec` |
| G3 | different source files | **avoided** — quote matmul-only |
| G4 | no same-file baseline | **closed** — built and run |
| G5 | fetch alignment | **closed** — 3 layouts, both sides |
| G6 | no output address in the ISA | **closed** — `out_base` + write-back + step 5 |
| G7 | READRES over-issued 4x | **closed** — one per bankgroup, 2,112 |
| G8 | every number measured on a blocking CPU | **closed** — 28.5x on O3; range fence re-measured 2026-10-03: fences = 22.7% of O3 PIM matmul (upper bound); range fence parked |
| G9 | granularity never measured on a real workload | **closed** — rerun 2026-10-03: **1.30x** vs the best `pim.dispatch` schedule that runs (32 then wait); 192-at-once aborts; token exhaustion found |
| G10 | one GEMV at a time — isolation built, sharing not | **OPEN** — deferred behind overlap |
| G11 | descriptor pointers bypass the MMU | **OPEN** — documented, deferred (no time) |
| G12 | no in-order host | **closed** — after the O3 fence fix (G13): BERT Minor 16.54x vs O3 14.13x, GPT-3 28.29x vs 18.95x (same vector unit) — Minor ahead on both; 3 blockers fixed |
| G13 | O3 `pim.fence` released its barrier at execute | **closed** 2026-10-02 (`ed427c0`) — O3 BERT 15.60x → 12.59x, GPT-3 19.51x → 16.24x (default O3); Minor larger on both; G12's O3 rows superseded |
| G14 | O3 leaked the token of a squashed PIM offload | **closed** 2026-10-02 (`c01ad8a`) — token returned on squash; no timing change |

Remaining are scope statements, not defects: SEQ=1 only (quote as *batch-1*,
never "BERT"); PIMony produces no values. Future work, none blocking: the ROB/LQ sweep on O3
(compare `simTicks`, NEVER `numCycles`), the SEQ crossover on the real
instruction, and the fence-vs-uncacheable A/B (G2 option 3).

## Binaries and runs from this pass

| binary | build | run dir |
|---|---|---|
| `bert_pim_zfh` | `-DPIM_GEMV -march=rv64gcv_zfh` | `m5out/bert_pim_zfh` |
| `bert_pim_flush` | + `pim_flush_vec` | `m5out/bert_pim_flush` (+`_rerun`) |
| `bert_pim_a16/a32` | + `-falign-functions=` | `m5out/bert_pim_a16`, `_a32` |
| `bert_blas_s1_fp16_phase` | `-DBLAS_FP16` (zfh via march) | `m5out/blas_s1_fp16_phase` |
| `bert_blas_a16/a32` | + `-falign-functions=` | `m5out/blas_a16`, `blas_a32` |
| `bert_cpu`, `_a16`, `_a32` | **`-DPIM_FP16`** (same file, PIM off) | `m5out/bert_cpu*` |

All with `-DPHASE_STATS`. Uncommitted in `bert.c` / `bert_blas.c`: the `PHASE()`
markers and `pim_flush_vec()`.

---

# G6 — WHAT WAS BUILT, AND WHAT IS LEFT (2026-09-18)

## The design, four decisions

1. **Results are a fourth command type.** `MemoryAccessType::WRITE`, issued the same
   way as MAC, READRES and D2GWRITE. Nothing new had to be invented.
2. **The wave is carried, not decoded.** `out_jbase[stream]` is written at MAC issue,
   where the value is already known, instead of recovering it from the returning
   readout's address. That is the ordinary controller idiom — attach metadata on the
   way out, look it up on the response, as an MSHR does — and it is *cheaper* than
   the 64-bit `readres_base[]` it sits beside.
3. **One 8-byte write per command, not four 2-byte ones.** A command's four outputs
   are consecutive indices (`bank = j % banks`), so they are one contiguous run,
   aligned to its own size and unable to straddle a chunk. Four separate writes into
   one 32-byte chunk would cost four write transactions for data the controller
   already holds together — inventing traffic the hardware never pays. Costs no time
   either: the stream is already blocked until all four readouts drain.
4. **Recorded at the drain, issued from `IssuePendingGemv`.** The drain has no
   room-check and no one-per-cycle limit; PIMony's queue accepts silently and drops
   on overflow, so a write issued there could vanish and hang `pim.wait` forever.

Output index from a readout: `j = wave*units + stream*banks + bank`, address
`out_base + j * 2`.

## Why `out_base` may be an ordinary pointer

Settled from the PIMony paper's CA command-encoding table: the **READRES row carries
`BG1 BG0` and nothing else** — no row, no column, no bank. Compare ACT-2 (`R10..R0`),
MAC (`C5..C0` + BG), H2GWRITE (`C5..C0` + `GB1 GB0`). A command with no destination
field cannot name a place in DRAM, so READRES is a pure readout onto the data bus and
the value is **already in the memory controller** before anyone asks where it goes.

⇒ the alternative design — a channel-local output layout mirroring `pim_store_vec` —
buys no fidelity, because there is no in-DRAM write-back path to prefer. It only
constrains the address and charges the host a gather.

⚠️ **The one unmodelled cost, for the limitations list:** the front end must take data
returned on one channel's data bus and re-issue it as a write on another's. A real
loopback plus a write-combining buffer. gem5 charges nothing for it. Same bucket as
the self-clean freebie: state it as a hardware contract, never claim it is free.

## Guards and contracts added

| what | rule | where checked |
|---|---|---|
| descriptor | 32 B aligned (its own size) | `pimony.cc`, `desc & 0x1F` |
| `out_base` | non-zero, multiple of `banks_per_group * result width` | `AddGEMVTransaction` |
| completion | `wr_pending == 0 && wr_out == 0` as well | `GemvTryComplete()` |

⚠️ **`aligned(32)` must go on the STRUCT TYPE, not the array variable.** On the
variable it aligns only element 0 and `sizeof` stays 24, so `g_desc[1]` lands at
offset 24. The device guard caught it on the first run and named the address.

⚠️ **A result write must be intercepted before the CPU write branch** in `ClockTick`.
It has no gem5 packet behind it, and `write_remain_[write_sub2orig_[addr]]` would
*insert* a zero entry and then call `write_callback_(0)`. Keyed on a `gemv_wr_addr_`
set, tested first.

## Counting traps (all three bit us; all three are avoidable)

1. `grep -c "| WRITE |"` counts **host** writes too — descriptor writebacks at
   `0x80001000`. Filter to the output region.
2. **DRAMsim3 posts writes**: the response returns on *acceptance*, not on reaching
   the array. The job completes correctly, but the tail of the write queue is never
   issued before exit and prints no `IssueCommand` line — 12 addresses were missing
   from the log and they were exactly the last 12 (`j` 708–764). ⇒ **never count
   result writes from the log**; use `num_writes_done` / `num_write_cmds`. Not a
   correctness hole: a read matching a queued write is answered from the queue.
3. The **`column: 0` MAC discriminator is no longer exact** — it read 234 where
   generation is 232. A MAC preempted *before* its first step resumes *at* column 0,
   which write traffic finally made happen. Use `num_comp_cmds`, which is resume-proof.

## Open

- **Spec decision, not a bug:** `pim.wait` returns when the writes are *accepted*,
  not when they have reached the array. Real posted-write buffers behave the same and
  rely on forwarding for correctness. Worth stating deliberately — an examiner will ask.
- **Step 5, the remaining work:** bert.c consuming the results — W2's three partial
  sums, `pim.fence.inv` on the output range (it has had nothing to invalidate until
  now), and the type contract: the device writes FP16, `y` is `float`. Nothing is
  corrupted today because PIMony moves no data, but the contract must be settled.

## Methodology: the layout sweep is no longer trustworthy as written

`-falign-functions=16` and `=32` produced **byte-identical** fetch counts — one layout
built twice — so "take the level two of three agree on" had no independent evidence.
Only no-flag vs aligned differ, giving two points and never a majority. The flags align
function *entries*; at `-O3` the hot loops are inlined and sit mid-function, where
neither a flag nor a linker script can reach.

**Use `fetch/inst` as the fairness gate instead.** Across six builds it explained a
144,000-cycle spread to within 0.1%:

```
                    ROI      fetch/inst
g6_a32           705,709        1.127
g6_a16           712,512        1.127
flush            737,116        1.200
a16              820,557        1.419
a32              821,202        1.419
g6               849,901        1.492
```

Close on both sides → comparable. Far apart → the cycles are not comparable, full stop.
This is what turned a "+15% regression" into a measurement error: `737,116` was a lucky
build, and the real pre-G6 level was ~820,900.

**Noise floor, measured honestly:** blocks with byte-identical fetch counts across two
builds (blocks 4 and 8, residual+layernorm, `quiesce = 0`) still differ +2.8% / −1.5%
in opposite directions. So ±3% is this setup's resolution with fetching held constant.

---

# G7 — READRES WAS OVER-ISSUED 4x · found 2026-09-18

**Ours, not PIMony's.** The sequencer issued one READRES per *bank*; the command
cannot name a bank. Correct is one per *bankgroup*: **8,448 -> 2,112**.

## Evidence

1. **The paper's CA table, READRES row.** Falling half is `- - - BG1 BG0 - -`.
   Bankgroup bits only. The Precharge row directly above it *does* carry
   `BA1 BA0`, in the positions READRES leaves blank — so the blank is meaningful,
   not an omission. A bank address structurally cannot reach the wire.
2. **PIMony's own reference generator**, `LLM.cc:216-231`, non-`ALL_BANK` path:
   `MAC(mac_start_addr); READRES(mac_start_addr);` — exactly one, at the MAC's
   own address.
3. **PIMony has no opinion on the count.** A READRES does three things in the
   whole controller: timing constraints (`channel_state.cc:582`), increment
   `num_readres_cmds` (`pim_controller.cc:878`), push one return-queue entry. It
   never touches `mac_states_` or any accumulator bookkeeping.

## Where the bug came from

`pim_controller.cc:794-801` (`DecodePIMTransaction`) folds (rank, bankgroup, bank)
into a 5-bit `num_comps` before nulling `addr.bank`, with the comment "encode
(num_comps-1) to rabgba bit". The sequencer's comment at `memory_system.cc:673`
read that as the command format — "unlike MAC the bank field here is
load-bearing". It is simulator-internal bookkeeping that never reaches the wire.

**The rule:** the CA table is the spec. Simulator-internal encodings are not, and
an in-code comment asserting otherwise is not evidence.

## The two costs, which must never be merged

- **PIMony's, inherited, reportable.** One READRES drains a bankgroup and occupies
  a full read burst slot: `request_size_bytes = device_width/8 * BL` = 2 x 16 =
  **32 B** (`configuration.cc:390`; note *device_width*, not `bus_width`). A
  bankgroup's payload is 4 x FP16 = 8 B — *inferred from Fig. 1's adder-tree-to-
  Result structure, not stated by the paper and not modelled by the simulator, so
  keep the hedge*. Belongs in the limitations section.
- **Ours, a defect, never reportable as a platform cost.** 4 commands where the
  format permits 1.

## The fix

`memory_system.cc`: `:269` and `:270` seed `banks_per_group`, change to 1; delete
the `bank * bank_stride_` walk at `:668-669` so the address is the MAC's own.
Every other consumer is a proportional counter and needs no change. Nothing
counted results: the write-back was already per *command* (2,112 x 8 B writes),
so that number does not move.

**Watch for in the first run:** a stream now frees after one readout instead of
four, so the next MAC on that bankgroup issues sooner. `engine_busy` still gates
on PIMony's MAC response and PIMony enforces its own `mac_to_readres` spacing,
but "sequencer feeds an engine sooner" is the shape of the MACINTR preemption
abort. That, not a completion miscount, is the failure mode to expect.

## The readout cost, re-measured · 2026-09-30

The rev-4 figure (+25% on the compute round, 248 -> 310 cycles) and the "second
result register per bank" recommendation built on it were taken at 4x over-issue.
Re-run: ladder rebuilt from current `main.c` (`tests/test-progs/pim_gemv/
pim_gemv_ladder`, 3-field descriptor), timing, log `m5out/gemv_ladder_now.log`.

- Counts: 232 MAC originals, **232 READRES** (was 928), 12 D2GWRITE.
- Stream cid0/r0/bg0, phase C rounds: 280, 846 (refresh), 360, 377, 274.
- Clean round 5487 -> 5767 decomposes exactly:
  `192 work | 22 drain | 33 READRES -> first PRECHARGE | 14 row change | 19 tRCD = 280`.
  Without the 33 it is 247 — rev 3's 248 no-readout round. That is the check.

⇒ **The readout stall is 27–33 cycles per round, about 11% (was 62, 25%).** The
second result register would buy about 11% of compute time, half the rev-4 claim.
It stays a recommendation for the PIM hardware; building it is out of scope.

The 360/377 rounds are a different effect, not part of this number: the result
write-back (9 WRITEs to OBASE, row 8272, in the same banks as the weights) lands
mid-round, forcing row changes and preempting MACs (24 resumes). **Open.**

## Also stale

`RESULTS_pim_gemv.md` carried 8,448 in three places. **Corrected 2026-09-19**,
along with its Amdahl paragraph, which G8 inverted.

---

# G6 STEP 5 — THE HOST NOW CONSUMES THE RESULTS · 2026-09-18

`pim_linear` used to end with `y[o] = b[o]`. Results were computed, written, and
discarded. Closed:

- results land in `pimout`, **FP16** — the device's own precision, host converts
- **W2's three partial sums** added on the host
- **`pim.fence.inv`** over the output range before read-back

The fence is load-bearing twice. Architecturally it is the invalidate. Its
`"memory"` clobber is also what stops `-O3` folding `pimout` to its zero
initialiser — the CPU never writes that array, and without the clobber the whole
read-back is dead code the compiler may delete. **Any future result-consuming
code has the same trap.**

## Verification

| check | result |
|---|---|
| `quiesce` | 8, unchanged |
| instructions | 192,886 -> 260,902; **all** growth inside blocks 1,3,5,7 |
| `pim.fence.inv` | **264** exactly — 72 / 24 / 96 / 72 for QKV / Wo / W1 / W2 |
| insts per output | **9.0** for 1-part matmuls (QKV, Wo, W1 agree to 3 digits), **16.2** for W2 |

The per-output figure is the real check: three independent matmuls landing on
9.0 says the read-back loop does exactly the work it should, and W2's 16.2 is
three partials loaded instead of one.

## Cost

```
blk 1 QKV    117,683 ->   167,701   (+50,018)
blk 3 Wo      72,520 ->   109,854   (+37,334)
blk 5 W1     121,881 ->   199,367   (+77,486)
blk 7 W2     357,189 ->   398,010   (+40,821)
            ---------------------------------
layer        849,901 -> 1,051,688  (+201,787, +23.7%)
```

Control blocks moved −3,872 total, inside the ±3% noise floor.

## THE ASYMMETRY, RESTATED — G6's headline was wrong

G6 recorded: *"getting data into PIM costs two orders of magnitude more than
getting results out."* That is not the finding.

```
device writes results back     0.77% of a layer
host consumes those results      24% of a layer
```

Both halves of the result path exist; only one is cheap. The **device's** side is
a rounding error, the **host's** side is not. Operand delivery (80% of CPU busy)
and result consumption (24%) are both host-side. The correct statement is that
PIM's own traffic is negligible at both ends, and every real cost of offload sits
on the host — which is a better finding, because it is an argument about where an
ISA should put its effort.

## Assumption on the record

The paper gives operand widths (16 x FP16 from the BLSA, 16 from the global
buffer) but **never a result or accumulator width**, including the DPSA and area
sections. FP16 is our choice, not a reading. Sensitivity is one line
(`kResultBytes` 2 -> 4) and would take result traffic from 0.77% to ~1.5%, so no
conclusion depends on it. The absence itself belongs in the limitations list.

## What this hands the range fence

Two measured per-line fence costs now, not one:

| path | lines/layer | instruction |
|---|---|---|
| operand | 672 | `pim.fence.cl` — 50,348 cyc measured |
| result | 264 | `pim.fence.inv` |

And a sharper argument than "fewer instructions": each per-line fence carries
`IsReadBarrier + IsWriteBarrier` (`decoder.isa:6672-6678`), so 264 of them are
264 ordering points. On an in-order CPU that is free and the barriers have never
been exercised; on O3 they forbid overlap by construction. **The range fence's
value is permission to overlap, not instruction count** — a micro-op expansion
that still serialises reproduces 74.9 cyc/line and buys nothing. The parameter to
sweep is how many line operations the range unit may have in flight; 1 reproduces
today.

## Next

- decoder entry: custom-0 funct3 `0x6`, rs1 = base, rs2 = length (bytes), funct7
  selects cl / inv / flush. Existing three untouched — they are the baseline.
- final-ISA recommendation, separate from the experiment: the three collapse into
  one R-type shape with **length 0 = one cache block**, which leaves every
  existing encoding valid. Build separately, measure, then propose the collapse.

---

# G8 — THE HOST MODEL WAS DOING MORE WORK THAN THE INSTRUCTION · 2026-09-19

Four findings, one thread: **every number in this file above was measured on a
CPU that stalls on every memory access**, and that turned out to matter more than
anything in the sequencer.

## G8.1 — range fence: fences are 23% of PIM matmul time on O3 · PARKED, upper bound only

**2026-10-03, Ayush: parked — build later if time permits.** Estimate ~2 days (gem5 change +
range test; then BERT on O3 and Minor), GPT-3 a few hours more. Placement: the load/store unit
(O3 already splits a multi-line access into per-line requests). Minor allows only 2 accesses in
flight (`executeMaxAccessesInMemory`, gem5 default, kept), so at most ~2x cheaper fences there.

**2026-10-03: RE-MEASURED on the fixed O3 (G13). The negative result below is WITHDRAWN.**
Same A/B, matmul blocks 1/3/5/7, cycles = `simTicks`/333.33:

```
                         fences on   fences off   fence cost   per fence   share of PIM matmul
timing                     874,065      809,779       64,286        68.1          7.4%
O3, broken fence (old)     208,743      198,214       10,529        11.2          5.0%
O3, fixed (ed427c0)        256,300      198,214       58,086        61.5         22.7%
```

On the fixed O3 a fence costs about what it costs on the blocking CPU (61.5 vs 68.1
cycles): no overlap left. The fence-free control was rerun on the fixed build
(`m5out/bert_pim_g7_nf_o3_f2`, 198,214 again), reproduced by Ayush 2026-10-03.

944 fences per layer, not 936: 672 vector cleans + 8 descriptor cleans + 264 result
invalidates (dcache CleanSharedReq + InvalidateReq). The old table also took O3 cycles from
`numCycles`, which drops `pim.wait` sleep (G8.3).

**58,086 is the most a range fence could save, not what it saves** — it still cleans or
invalidates every line. Not measured yet: that saving, Minor (needs a `bert_pim_g7pt_nf`
build; g7 crashes on Minor), GPT-3.

```
cd gem5/m5out && for d in bert_pim_g7_timing bert_pim_g7_nf_timing bert_pim_g7_o3 bert_pim_g7_nf_o3 bert_pim_g7_o3_f2 bert_pim_g7_nf_o3_f2; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^simTicks /{t[b]=$2} END{printf "%-22s %d\n", d, (t[1]+t[3]+t[5]+t[7])/333.33}' $d/stats.txt; done
```

The text below is the 2026-09-19 conclusion, kept as the record of what the broken fence implied.

G2 concluded: *"they do NOT pipeline. Each pays full latency ⇒ this is the
measured case for a range fence."* That conclusion does not survive.

`cpu/simple/timing.cc:285-291`: TimingSimpleCPU sends a request, sets
`_status = DcacheWaitResponse`, and only calls `advanceInst` from
`completeDataAccess`. **Every memory operation blocks** — loads too, not just
fences. "They do not pipeline" was true of everything on that CPU.

A/B with a control build (`-DNO_PIM_FENCE`, below), 936 fences per layer:

| | fences on | fences off | delta | per fence | share of layer |
|---|---|---|---|---|---|
| timing | 1,051,688 | 988,712 | 62,976 | 67.3 | 5.99% |
| **o3** | **135,414** | **127,183** | **8,231** | **8.8** | **6.08%** |

**On O3 the fences overlap: 67.3 -> 8.8 cycles each, 7.6x.** The mechanism
argument for a range instruction — "give hardware permission to overlap" — is
dead, because the hardware already does. What remains is the residual 8.8
cyc/line, and a walker cannot make the cache work free.

⚠️ The share of the layer is unchanged (5.99% -> 6.08%) because O3 sped up
everything by about the same factor. Absolute prize fell 63k -> 8k; proportional
cost did not move. Quote the proportional figure only with the absolute beside
it, or it reads as if nothing changed.

**Decision: do not build it.** Write the negative result instead — *an ISA
extension that looks strongly justified on an in-order model can evaporate on a
realistic one.* Same family as G5: measure on the machine you are claiming about.

**The control build is a trap worth keeping.** `-DNO_PIM_FENCE` drops the
instruction but keeps the `"memory"` clobber. Deleting the whole wrapper would
delete the read-back with it (see G6 step 5) and measure two things at once.

Note also: the original bundling of ordering into every CMO was deliberate and is
right for the dispatch path — one line, one ordering point. RISC-V's own
`cbo.clean` (`decoder.isa:1396`) carries no `inst_flags` and leaves ordering to a
separate `fence`. Ours differs on purpose. That choice is not what this finding
overturns.

## G8.2 — `pim.gemv` runs on a pipelined CPU · first time

`quiesce = 8`, 672 cleans, 261 invalidates on O3. The whole offload — dispatch,
sequencer, interrupt completion, `pim.wait` — works out of order. `PIM_GEMV` is
in the `PIM_CMD` mask (`request.hh:285`), so the LSQ payload bug never bit.

(261 vs 264 invalidates is the documented CMO-stats gotcha: three merged into an
in-flight MSHR. Cleans came in at exactly 672.)

## G8.3 — THE SPEEDUP IS 28.5x ON O3, not 8.24x

> ⚠️ **2026-09-30: the MECHANISM below is contradicted by Minor (G12).** "A better
> host widens PIM's advantage" still holds. "Because out-of-order overlaps PIM's
> independent host work" does not: in-order Minor widens it just as much (16.54x vs
> 17.53x for O3 with the same vector unit). The cause is the memory-bound baseline
> gaining little from ANY better core, not out-of-order issue.

```
                      timing CPU        O3 CPU     (cycles from simTicks)
CPU-only (OpenBLAS)    7,198,834      3,189,369
PIM                      874,066        208,743
                    ---------------------------
matmul-only                8.24x         15.28x
whole layer                7.46x         14.72x
```

⚠️ **CORRECTED 2026-09-19, same day.** First reported as 28.51x/25.25x from
`numCycles`. On O3 that stat counts only active cycles, so PIM's 96,978 cycles
asleep in `pim.wait` were dropped while the never-sleeping baseline kept all of
its own. Inflation ~1.9x. The trap was already written down twice — in
`project_xsgem5_rejected_host_config` and in the design record artifact — and was
walked into anyway. **Derive every cross-CPU number from `simTicks`.**

**O3 helped PIM 4.5x and the baseline 2.3x.** IPC says why: baseline **0.43**,
PIM **1.93**. At batch 1 the CPU baseline streams 28 MB of weights with no reuse
— memory-bound, and out-of-order cannot fix memory-bound. PIM's remaining host
work is staging operands and reading results back: hundreds of *independent*
memory operations, which is exactly what OoO overlaps.

**A better host WIDENS PIM's advantage** (~2x here). That is the opposite of the
usual expectation and it is the interesting result: it lands directly on the parked
"best host for async PIM" question, with a mechanism rather than a hypothesis.

**Baseline verified vectorised**, per `reference-openblas-riscv-targets`: the
kernel actually called, `shgemv_n`, has 24 vector instructions in its body (5,367
across the binary). IPC 0.43 is a vectorised memory-bound kernel, NOT the silent
scalar fallback. That check had to pass or the number was a mirage.

## G8.4 — the SE/FS mode tax on O3 is ZERO · calibrated

The baseline is SE, PIM is FS. The 0.36% tax was calibrated on TimingSimpleCPU
(2026-08-25) and had never been checked on a pipelined CPU. Same binary pair,
PIM off, O3 both sides:

```
FS   35,582,088 cycles      instructions 49,651,305
SE   35,582,184 cycles      instructions 49,651,305   <- identical, so same code
     ----------------
            96 cycles  =  0.00027%
```

Lower than on timing, because the tax is a fixed entry cost and O3 shrinks
everything around it. **The mode asymmetry cannot account for any part of 28.5x.**

## What is still open on 28.5x

Exactly one: **O3 here is gem5's default configuration, not a modelled real
core.** The host sweep (ROB/LQ, anchored to PEI ISCA 2015) was already planned;
this finding is the reason to run it, because the result *is* that host choice
dominates. Until then quote 28.5x as "on an out-of-order host", never as a
property of the instruction.

## Binaries and runs

| binary | build | runs |
|---|---|---|
| `bert_pim_g7` | `-DPIM_GEMV -march=rv64gcv_zfh -DPHASE_STATS` | `m5out/bert_pim_g7_{timing,o3}` |
| `bert_pim_g7_nf` | + `-DNO_PIM_FENCE` (control) | `m5out/bert_pim_g7_nf_{timing,o3}` |
| `bert_blas_s1_fp16_phase` | `-DBLAS_FP16` | `m5out/blas_s1_fp16_phase{,_o3}` |
| `bert_tax_fs` / `bert_tax_se` | `-DPIM_FP16`, ±`-DBAREMETAL` | `m5out/tax_{fs,se}_o3` |

---

# G9 — pim.gemv VS pim.dispatch, MEASURED AT LAST · 2026-09-19

## ⚠️ RERUN 2026-10-03 ON THE FIXED O3 — THE RATIO AND "THE STRONGER HALF" BOTH CHANGE

```
matmul blocks 1/3/5/7, cycles = simTicks/333.33       pim.dispatch    pim.gemv    ratio
1 at a time, then wait    (bert_disp_o3_f2)             2,934,992      256,300    11.45x
32 at a time, then wait   (bert_disp_w32_o3_f2)           333,455      256,300     1.30x   <- quote
all 192 at once           (bert_disp_w192_o3_f2)        PIMony aborts after 49 MACs sent
```

- **32 at a time runs.** Built from commit `7066456`'s `bert.c` (that rebuild is
  byte-identical to `bert_disp`) with only `-DPIM_WAVE=32`; config.ini identical to
  `bert_disp_o3_f2` bar the binary. All 2,112 MACs sent, each address once, 66 per engine,
  `m5_exit`. The next MAC on an engine arrives >= 531 DRAM clk after the previous one there
  last started; the longest MAC is 64 steps x tCCD_L 4 = 256 clk ⇒ no busy-engine
  collision. 34 MACs were paused by a host read/write and resumed — PIMony's DPSA rule
  (`pim_controller.cc` ~129–200), not an error.
- **The old hang was ours.** That run predates the token fix below: 66 batches need 66
  tokens and `allocPimToken` only counted up (`bb2f404`, `isa.hh:150`), so batch 65 waited
  on token 64 forever. The fixed run's log reads 2,104 MAC issues at the end of batch 65
  (2,080 sent), so the old "2,109 of 2,112" was a whole batch short, not 3 commands.
  Inferred — the old log is gone.
- **192 at once still aborts** (`PIM State: CLOSED, Command: MACINTR`): a second command
  reaches an engine 190 clk after the first one started, inside its 192-clk MAC.
- **What stands of the argument.** `pim.dispatch` still does less — no fill, no readout, no
  write-back, because no instruction exists for them — and its completion fires on a
  batch's last MAC only (`memory_system.cc`, `pim_last`). So the host must know the engine
  count, and 32-at-a-time is safe by timing, not by construction (read from the code, not
  measured). `pim.gemv`'s sequencer gates every MAC on `engine_busy` and counts every
  drain. **The case for `pim.gemv` over `pim.dispatch` is completeness and safety, plus
  1.30x — not 13.9x.**

```
cd gem5/m5out && for d in bert_disp_w192_o3_f2 bert_disp_w32_o3_f2; do echo "$d  MACs sent: $(grep -ac 'AddTransaction(PIM).*[|] MAC [|]' $d/run.log)"; grep -a -E 'Exiting|PIM State' $d/run.log | sed 's/\x1b\[[0-9;]*m//g'; done
for d in bert_disp_o3_f2 bert_disp_w32_o3_f2 bert_pim_g7_o3_f2; do awk -v d=$d '/Begin Simulation Statistics/{b++} /^simTicks /{t[b]=$2} END{printf "%-22s %d\n", d, (t[1]+t[3]+t[5]+t[7])/333.33}' $d/stats.txt; done
python3 ../configs/scratch/progs/engine_gaps.py bert_disp_w32_o3_f2/run.log    # 32 engines, 66 each, 34 paused, min gap 531
python3 ../configs/scratch/progs/engine_gaps.py bert_disp_w192_o3_f2/run.log   # min gap 190
```

Binaries `bert_disp_w32` / `bert_disp_w192` in `configs/scratch/progs/`. The run logs keep
only lines matching `grep -a -E "[|] MAC [|]|Exiting|PIM State|panic|fatal"`.

Everything below is the 2026-09-19 record, kept as it was.

---

The granularity argument had been carried since August as **arithmetic on a
microbenchmark**: 78 cyc/dispatch from 16 back-to-back dispatches, multiplied
out to "~128x". That number was issue-time against per-engine work — a
*utilisation* figure, not a speedup — and it was computed on **attention**, which
the settled workload partition says PIM does not run. `pim.dispatch` had never
executed a real workload.

It has now. `bert.c -DPIM_BY_DISPATCH` expands the same six matmuls into 2,112
`pim.dispatch` instead of 8 `pim.gemv`. Same file, same weight layout, same host,
same baseline. **The PIM does identical work: 2,112 MAC commands either way.**

## THE RESULT — o3, one BERT layer, cycles from simTicks

```
                                pim.dispatch      pim.gemv      ratio
TIME
  matmul sections                 2,895,695       208,743       13.9x
  whole layer                     2,919,736       232,162       12.6x
WHERE IT GOES
  CPU busy                        1,069,642       135,184        7.9x
  CPU asleep in pim.wait          1,850,094        96,978       19.1x
ROUND TRIPS
  completions waited on               2,112             8         264x
  cycles per round trip                 876        12,122
HOST INSTRUCTIONS
  total in ROI                    1,117,225       260,908        4.3x
  extra vs pim.gemv, per command        405             —
WORK DELIVERED
  MAC commands                        2,112         2,112        same
```

**63% of the dispatch layer is the CPU asleep**, waiting on 2,112 separate
completions. And the 856,317 extra instructions are the **trap handler**: ~405
per command, because it scans all 64 token slots on every completion. Per-command
offload means per-command interrupt handling, and that dominates.

⚠️ **`pim.dispatch` does LESS and still loses.** It has no instruction for the
global-buffer fill (D2GWRITE) or the readout (READRES), so it never fills the
buffer, never reads a result, never writes one back. `pim.gemv` does all three.
Two of PIMony's five commands were never given an instruction — that is the
finding, not a flaw in the experiment. **Present the asymmetry, do not hide it.**

## THE STRONGER HALF: two of three schedules DO NOT RUN

⚠️ **Superseded 2026-10-03:** only "all 192 at once" fails. "32 then wait" runs on the
fixed build; its hang was our token bug (see the rerun at the top of G9).

| schedule | outcome |
|---|---|
| all 192 at once | **PIMony ABORTS** — `PIM State: CLOSED, Command: MACINTR`, after 143 commands |
| 32 at a time, then wait | **HANGS** at 2,109 of 2,112 |
| 1 at a time, then wait | works |

A MAC to a busy bankgroup is a **RESUME**, not queued work. The sequencer never
trips this because it gates on `engine_busy` — it sees every completion. **The
host cannot see them**, so it must guess, and guessing wrong loses a command and
deadlocks the completion count.

This is a *correctness* argument and it is harder to attack than a ratio: tune
the baseline all you like, two of the three schedules a programmer would
naturally write still do not run. Serialising is not a handicap we chose — it is
the only safe schedule available to a host with no per-engine completion signal.

Reproduce the failures with `-DPIM_WAVE=32` or `=192`.

## THE THIRD FAILURE: TOKEN EXHAUSTION — and it was a real bug of ours

The serialised path hung at **65 MACs**. Cause, in `arch/riscv/isa.hh`:

```c
static constexpr unsigned NumPimTokens = 64;
uint64_t allocPimToken() { return pimNextToken++; }    // never wraps, never frees
```

Token 64 falls outside the scoreboard, its completion can never be observed,
`pim.wait` sleeps forever. The CPU robustness audit had listed "token exhaustion
@64 = next crash". This was it.

**64 is the width of the completion register**, not a chosen constant: the device
reports completions as a 64-bit MMIO bitmask (`PIM_DONE`), one bit per token, and
the CPU mirrors it in a `std::bitset<64>`.

**Fixed** (`isa.hh` + `decoder.isa`): a free-list allocator, and `pim.wait`
returns the slot when it consumes a completion. Safe because a slot is only
returned *after* its job completed, so nothing in flight can land on a reissued
slot — the reuse-after-complete rule PCIe and NVMe state for their tags. The ASID
gate is untouched; a foreign token is deliberately NOT freed.
Limit moves from **64 ever** to **64 outstanding at once**.

⚠️ `allocPimToken` now returns `NoPimToken` when full and **nothing checks it**.
Unreachable today (8 outstanding max) but it is a real hole — see below.

## THE DIAGNOSIS, from the literature survey (Claude Desktop, 2026-09-19)

> The bug is not that 64 tokens is too few. **Three independent quantities were
> bound to the same number:** *naming* (how many jobs can be told apart),
> *capacity* (how many the device can hold), and *reporting* (how completions get
> back). The token was used as an **index into the report structure**, so report
> width = tag width = capacity. **Essentially no production interface does this.**

**The sentence to write in the thesis: capacity and naming are separate problems
and need separate mechanisms.**

In every production interface the completion **carries its identity as data, not
as a position**:

| | naming | how completions return | exhaustion policy |
|---|---|---|---|
| **ours** | token = bit index | 64-bit bitmask | none — silently invalid |
| Intel DSA / ENQCMD | PASID; no per-job tag | completion record at a submitter-chosen **address** | **enqueue fails, ZF=1, retry** |
| NVMe | 16-bit CID per SQ | completion **queue entry containing the CID** + phase bit | host cannot advance past head |
| PCIe | tag pool, 5→8→10→14 bits | packets **carrying** the tag | reuse only after completion |
| AXI | `AxID`, impl-defined | responses carry the ID | cannot exhaust — VALID/READY stalls |
| Arm SMMUv3 | StreamID/SubstreamID = identity | **event queue**, prod/cons indices + wrap bit | producer waits |
| Vulkan timeline semaphore | **none** — 64-bit monotonic counter | wait for "counter >= N" | cannot exhaust |
| RoCC | `rd` register number | core scoreboard + busy | **core stalls** |
| CV-X-IF | `X_ID_WIDTH` (CVA6 default 3 → 8) | issue-interface accept/reject | reject at issue |
| IXIAM (RISC-V, gem5) | none — 1 job per accId | status register | **request silently dropped**, software must CHECK |

## CITATIONS

- Intel DSA Architecture Specification, doc. 341204 rev. 006, §3 — work queues,
  portals, WQCFG threshold. **ENQCMD returns ZF=1 = retry**: the only ratified
  vendor-ISA precedent for a *dispatch instruction that can fail*.
- Intel ISA Extensions and Future Features Programming Reference, doc. 319433 —
  ENQCMD / ENQCMDS.
- Kuper et al., *A Quantitative Analysis and Guidelines of Data Streaming
  Accelerator in Modern Intel Xeon Scalable Processors*, ASPLOS 2024.
- Asanović et al., *The Rocket Chip Generator*, UCB/EECS-2016-17, 2016 — RoCC.
- OpenHW Group, Core-V eXtension Interface spec; CVE2 issue #306 (two in-flight
  instructions sharing an ID — **the exact aliasing hazard, in a real interface**;
  cite as an issue-tracker item, not a spec statement).
- Arm SMMUv3 Architecture Specification, IHI 0070 (D.a+), §6.3–6.4 — queue
  discipline, wrap bit. Linux commit `b4163fb3` — the empty→non-empty interrupt
  hazard.
- Arm AMBA AXI Protocol Specification, IHI 0022.
- NVM Express Base Specification 2.x — SQ/CQ model. ⚠️ **section numbers unverified.**
- PCI Express Base Specification r5.0 §2.2.6.2 (10-bit tags); r7.0 §7.5.3.4
  Table 7-21 (enable bits). Also: changing Extended Tag Field Enable with
  non-posted requests outstanding is **undefined** — the same drain-before-
  reconfiguring rule our ASID recycle needs.
- VK_KHR_timeline_semaphore, core in Vulkan 1.2; Khronos, *Vulkan Timeline
  Semaphores*, Jan 2020. HSA Platform System Architecture Spec — AQL, signals.
- **B. Peccerillo, E. Cheshmikhani, M. Mannino, A. Mondelli, S. Bartolini,
  "IXIAM: ISA EXtension for Integrated Accelerator Management", IEEE Access
  vol. 11, pp. 33768–33790, 2023, doi:10.1109/ACCESS.2023.3264265.**
  ⇒ **Nearest published neighbour to this whole thesis** — RISC-V extension in
  gem5 with RESERVE / EXEC / ISBUSY / AFENCE / RELEASE and a user-space interrupt
  module. Read it properly. Its exhaustion policy is weak (FIFO full ⇒ request
  discarded with no indication; software must CHECK), and its interrupt table is
  a fixed 256 entries indexed by accelerator ID, justified by one owner per
  accelerator.
- J. Cong, M. A. Ghodrat, M. Gill, B. Grigorian, G. Reinman, *Architecture Support
  for Accelerator-Rich CMPs*, DAC 2012, pp. 843–849 — reject **with a wait-time
  hint** returned to the core. Extended: ACM TECS 13(4s), 2014, art. 131.

**Negative result, verify before asserting:** no RISC-V International task group
found on accelerator completion signalling or async offload. Check the in-process
dashboard on wiki.riscv.org before writing "none exists". Ratified and adjacent:
Zawrs, AIA (Smaia/Ssaia), RISC-V IOMMU 1.0 (identity tags only). The N extension
for user-level interrupts was never ratified — already used as justification.

## THE OPEN DECISION, deliberately DEFERRED

**Does `pim.wait` name a job, or a point in a stream?** Everything else follows.

- **A — name a job** (what we have). Needs recycling (done), a full-signal, and a
  fairness policy. Carries the owner, which matters: with several processes
  sharing the engines, "job 700 is done" must say *whose*.
- **B — wait on a count** (Vulkan timeline). No tag space, cannot exhaust. But a
  bare counter **carries no owner**, so it does not fit the multi-process/ASID
  story as-is. The fitting variant is *one counter per context* — making the
  finite resource **contexts** (few, bounded) rather than **jobs** (many).

**Why deferred, 2026-09-19:** `pim.gemv` uses **8** tokens per layer against a
ceiling of 64, so every number in this file is identical either way. Building the
completion queue touches five things on the completion path — device ring +
producer index + phase bit, MMIO, trap handler, CPU-side unclaimed-completion
table, `pim.wait` — and that path took weeks to get right the first time. It
competes with the overlap evaluation, which *would* move a number (41.8% of the
layer is the host asleep).

**So: state it in the design chapter, do not build it.** The citations above carry
the argument, and the exhaustion becomes evidence for the main claim — per-command
offload needs 2,112 tokens per layer and dies; per-matmul offload needs 8.

**If it is built later, the shape is:** completions arrive as a small ring of
*entries* (`{token, asid}`), sized to hold **unclaimed** completions rather than
one slot per job that could ever exist. `pim.wait` blocks, so most completions are
claimed immediately and the ring stays nearly empty. Capacity then becomes a
number you choose, not a register width you inherited. ⚠️ Implement the SMMUv3
hazard fix from the start: re-check the ring before leaving the handler, or
entries added mid-drain are lost forever.

**Remaining limits of the recycle fix, all unaddressed:**
1. 64 outstanding at once — a shared global pool, not per process. 8 processes at
   8 each fills it.
2. No fairness — one process can take all 64 and starve the rest. The cited fix is
   Intel's limited-vs-unlimited portal: cap what any one ASID may hold. Note this
   **divides** capacity, it does not create it.
3. Abandoned tokens leak — dispatched and never waited on ⇒ never returned. The
   fix is the PIM shootdown on ASID teardown, already designed and not built.
4. `NoPimToken` is returned and never checked. RISC-V has **no flags register**, so
   an ENQCMD-style retry must go in `rd` as a sentinel — `pim.dispatch` already
   writes `rd`, so the encoding is free. *That is a genuine RISC-V-specific
   contribution paragraph, not a rehash of ENQCMD.*

## Files touched

| file | change |
|---|---|
| `bert.c` | `-DPIM_BY_DISPATCH` path; `PIM_WAVE` batch size (1 and 32 run, 192 aborts) |
| `arch/riscv/isa.hh` | free-list `allocPimToken` + `freePimToken` + `pimInUse` |
| `arch/riscv/isa/decoder.isa` | `pim.wait` frees the slot on `PIM_DONE_MINE` |
| `tests/test-progs/pim_issue/main.c` | 32 engines, `NDISP` settable, per-engine row offset |
| `configs/pimony/fs_pim_issue.py` | took `argv[2]` for the binary — **it was hardcoded**, so a whole sweep silently ran one binary |

## Microbenchmark, for the mechanism in isolation (o3)

Same MAC commands, issued both ways. `pim_issue/iss_N` vs `pim_gemv/gemv_N`:

```
commands   dispatch    gemv    ratio
    16        4,848    3,423    1.4x
    32        8,016    3,429    2.3x
    64       14,136    4,254    3.3x
   128       26,424    5,493    4.8x
   192       38,715    7,860    4.9x
```

`pim.gemv` barely moves from 16 to 32 — the sequencer absorbs them. The gap grows
because dispatch pays per command and gemv pays per instruction. **N=192 is one
real Wq matmul**, so that row is not a toy size.

⚠️ Two traps cost an afternoon here, both worth remembering:
1. `fs_pim_issue.py` **hardcoded its binary**, so five different builds all ran the
   same one and produced five identical results. *Always print the bootloader.*
2. Spreading engines across channels put two operands in **one 64 B cache line**
   (channel stride is only 32 B) and tripped `MSHR::promoteWritable`. Fixed with a
   per-engine row offset. Engine identity is unaffected by row bits.

---

# G10 — THE ISOLATION IS BUILT, THE SHARING IS NOT · OPEN, found 2026-09-19

`AddGEMVTransaction` (`memory_system.cc:412`):

```c
if (gemv_.active) return false;                     // busy: retry is correct
```

**One `pim.gemv` at a time.** A second gets a busy/retry and spins until the
first finishes.

## Why that is a hole

The justification on record is throughput — one GEMV already spans all 32
streams, so a second adds none — **and that reasoning is correct**. But it says
nothing about **latency or fairness across address spaces**. A long GEMV from one
process blocks a short one from another for its whole duration: no preemption,
no fairness, no bound.

Meanwhile the entire PASID/ASID line of work — per-token owning ASID, the
`pim.wait` foreign-token gate, the 2-process test — exists to let several
processes share the device. **So the isolation is built and the sharing is not.**
An examiner who reads both sections will find this.

## It is OUR limit, not the hardware's

Worth being precise, because it decides where the fix goes. The hardware
constraint is only the per-bankgroup rule: a MAC to a busy bankgroup is a RESUME.
The one-job-at-a-time rule is a line in our sequencer. **The fix belongs in the
sequencer, not the device.**

## The shape of a fix, unbuilt

The mechanism looks cheap: the sequencer already issues at most one command per
DRAM cycle and gates every issue on `engine_busy`. Multiplexing means holding N
job records instead of 1 and choosing whose next command to emit. The gate
already prevents collisions — that is what it is for. A command is ~200 cycles,
so round-robin at command granularity gives fine-grained sharing **with no
preemption at all**.

Awkward parts, in order of likely pain:
1. `engine_busy` becomes shared state across jobs, not per-job.
2. Completion accounting is per-job already (tokens), but `GemvTryComplete` and
   the response handler both assume a single `gemv_`.
3. The policy — round robin, weighted, priority — is the actual design question.
   The mechanism is not.

PIMony also has **MACINTR**, real hardware preemption, which exists so a host
request can interrupt a running MAC. Not needed for command-granularity
round-robin, but it is there if a scheme wants it.
⚠️ A MAC unit's accumulator **cannot be saved or restored** — READRES is the only
way out and there is no reload. So preemption is only safe at points where the
accumulator is expendable or already read out. Any scheme must respect that.

## Cost and priority

Survey: an evening. Build: a **few days**, mostly because the completion path is
where subtle bugs cost days rather than hours. The 2-process test from the PASID
work is the vehicle for measuring it.

**Deferred behind the overlap evaluation, deliberately.** Overlap is the month-1
critical-path item, is worth 41.8% of the layer, and does not depend on sharing.
There is also a dependency the other way: **if host-side overlap reclaims most of
that 41.8%, the case for multiplexing several PIM jobs changes.** Do overlap
first, then revisit this with that number in hand.

**If it is not built, state it as a limitation** — "the sequencer serves one job
at a time; multi-tenant fairness is future work" — with the mechanism sketch
above, which shows it was understood rather than missed.

## Reading, when it is picked up

A prompt for this survey was drafted 2026-09-19. The areas: accelerator sharing
between address spaces; scheduling granularity and preemption when the device
holds unsaveable state; GPU multi-tenancy as the mature analogue (MPS, MIG,
time-slicing) and which of it transfers to 32 command streams with no context
save; QoS in memory controllers, since this sequencer *is* a memory-side arbiter
competing with ordinary CPU traffic. Expect the memory-side-accelerator sharing
literature to be thin — establish how thin, because that itself is citable.
Edges already touched in G9: Intel DSA's portal thresholds, Cong et al.'s
wait-time hints (DAC 2012).

# G11 — THE DESCRIPTOR'S POINTERS NEVER SEE THE MMU · OPEN, found 2026-09-29

## The hole

The MMU checks only the address the instruction itself sends out, `EA`. For
`pim.gemv` that is `rs1`, the descriptor's address (`decoder.isa`, `ea_code={{
EA = rvZext(Rs1); }}`). The three addresses **inside** the descriptor, `w_base`,
`v_base` and `out_base`, are read by the memory side as plain bytes
(`pimony.cc:gemvFetchComplete`, `memcpy` then straight into `enqueueGEMV`). No
page-table lookup happens in between.

`pim.dispatch` has no such gap: its only address is its `EA`.

Today software writes **physical** addresses into the descriptor (D13 comment),
so a process can name any physical memory, including another process's. This
undercuts the ASID/PASID isolation of Steps 4.1–4.3 and G10.

## The rule that decides the fix

What matters is not where the PIM sits (the memory controller) but **who produces
the addresses**. The sequencer generates every address from the descriptor, so it
behaves like a DMA engine. The CPU's MMU is enough only if every address the PIM
uses has been through it.

## Chosen shape, unbuilt: check on the CPU side (no IOMMU)

1. The descriptor holds **virtual** addresses.
2. **The CPU reads the descriptor**, not the memory side. If the memory side
   re-read it after the check, software could rewrite it in between (TOCTOU).
3. The CPU translates all three pointers.
4. **Range check.** One translation covers one page, but a buffer covers many,
   and physically the next page can be anywhere. Each buffer must sit in **one
   huge page** (Sv39 1 GiB, as the S-mode tests already map). The CPU computes
   each buffer's last byte from `dot_steps`/`num_outputs` and checks that it is in
   the same page as the start. Otherwise it faults before anything reaches the PIM.
5. The CPU sends **only checked physical addresses**.

The rejected alternative is an IOMMU beside the sequencer, which holds the page table and
checks each address as it is used. It is sound, but it puts a whole translation unit on the memory
side.

## Why it costs 1–2 weeks

- **Transport, the main cost.** A request carries 128 b: a 64 b address plus
  64 b `_extraData` (`request.hh:469`). That is why the descriptor exists. Three
  checked PAs plus the shape don't fit, so the CPU→PIM protocol changes, not
  only the instruction.
- **Instruction.** `pim.gemv` becomes a macro-op: a descriptor load, three
  translations, the range checks and the send. There's precedent for
  multi-access RISC-V macro-ops (`insts/amo.hh`, `insts/zcmp.hh`).
- **O3 risk.** Every earlier O3 bug in this model (payload flag, token replay,
  squash-after) cost days.

The estimate comes from a code trace, not a prototype.

## Priority

**Deferred: no time** (2026-09-29, about 2 months left). Not on the critical path:
host sweep and the minor blocker come first. **If it is not built, state it as a
limitation and a hardware-contract requirement** using the five steps above. They show the
hole was understood, not missed.

# G12 — THE IN-ORDER HOST (MinorCPU) · CLOSED 2026-09-30

⚠️ **2026-10-02: every O3 figure in G12 is superseded** — O3's `pim.fence` was broken (G13).
The Minor figures and the three blockers stand. "Out-of-order adds 6–12%" is withdrawn:
after the fix Minor shows the larger speedup on BERT too. Numbers: top block.

## Why Minor, and what it tests

TimingSimple → O3 changed two things at once: the core became pipelined **and**
out-of-order. G8.3 credited the widening (8.4x → 15.3x) to out-of-order overlap of
PIM's leftover host work. Minor is pipelined but in-order, so it separates the two.
The thesis compares Minor and O3; TimingSimple is a first pass, mentioned briefly.

## THE RESULT — BERT layer, matmul blocks 1/3/5/7, cycles from simTicks

```
                                   baseline        PIM    speedup
Minor                             6,189,410    374,130    16.54x
O3 with Minor's vector unit       3,623,447    206,714    17.53x   <- the comparison
O3, gem5 default                  3,194,329    204,733    15.60x
TimingSimple (g7)                 7,370,634    874,066     8.43x
```

Minor helps PIM **2.34x** over TimingSimple and the baseline only **1.19x**; O3 helps
them 4.27x and 2.31x. The widening is already complete on the in-order core. The
baseline is memory-bound (matmul IPC 0.21 Minor, 0.40 O3), so a better pipeline buys
it little. PIM's leftover host code is latency-bound scalar work that any pipeline
speeds up (busy-time IPC 0.77 Minor, 1.96 O3).

⚠️ **Amended 2026-09-30 by GPT-3 (section below).** "A better core buys a memory-bound
baseline little" holds for pipelining (TimingSimple → Minor), not for out-of-order:
O3 keeps more misses in flight and speeds the BERT baseline up 1.71x, the GPT-3 one
2.12x.

**Out-of-order adds 6–12%** on top (layout sweep below). Report it as a range — and
only for BERT: on GPT-3 the order flips (section below).

## The comparison had to be matched

gem5's default Minor has **one** FP/SIMD unit at opLat 6; default O3 has **four**
SIMD units at opLat 1. In the matmul blocks the baseline is 72% vector ops
(SimdUnitStrideLoad / SimdFloatMult / SimdFloatReduceAdd) and PIM's host code has
**none** (66% IntAlu, ~10% scalar FP: FloatCvt, FloatAdd). Default-vs-default
therefore flatters Minor. O3 re-run with Minor's vector unit — `fs_bert.py` /
`se_bert.py` argv `6 1` (config.ini: FUList05 count=1 opLat=6): baseline +13.4%,
PIM +1.0%, 15.60x → **17.53x**. That matched pair is the in-order vs out-of-order
comparison.

Still an **upper bound** on the out-of-order gain: Minor's single unit also runs
scalar FP at 6 cycles, which the matched O3 kept on its fast FP units. Matching
that too could only slow O3's PIM side and shrink the gap.

## Three blockers — simulator plumbing, all fixed

1. **PIMony fabricated a read completion for address 0**
   (`readComplete(): Assertion p != outstandingReads.end()`, tick 530,342).
   `MemorySystem::AddTransaction` splits every CPU access into two halves (own
   channel + partner channel) and tracked them in single-slot maps keyed by
   **address**. The walker's 8 B read of root_table[2] (0x8004A010) and the L1D fill
   of the same line (0x8004A000) share the partner half 0x8004A020; the second
   overwrote the first, DRAMsim3 merged and returned both, and the orphan's
   `operator[]` lookup produced 0. **Fix:** one record per access + a FIFO of ids per
   half (`read_split_`/`read_half_q_`, `RetireHalf()`, `memory_system.cc/.h`); an
   unknown half is now `spdlog::error` + exit. Rebuild `libdramsim3.so` only.
   **Timing-neutral, verified:** `se_bert.py o3 bert_blas_fair` rerun → simTicks
   bit-identical (`m5out/bert_blas_fair_o3_fix`). Every collision crashed the old
   code, so no completed run was affected.
2. **Instruction page fault at the `sfence.vma` after `csrw satp`** — surfaced as
   `fatal: Unable to find destination for [0:0x40] on system.membus`.
   `--debug-flags=Faults`: `Fault (Address, 12)` at tick 587,745, PC 0x8000032e.
   setup_paging stores the PTEs and switches satp at once. Minor's store buffer
   still held the stores (line fill pending) when the uncached walker read the
   table → PTE 0 → fault → M-mode (medeleg 0) → mtvec 0 → fetch from PA 0.
   O3 escapes because its commit waits for all stores to write back before any
   non-speculative instruction; Minor has no such rule and gem5's sfence.vma is no
   barrier. (The order csrw-then-sfence is also backwards per the privileged spec;
   Linux fences first.) **Fix:** the page table is preloaded in the image
   (`bert.c`, `GIGAPAGE`, stores kept) → binary **`bert_pim_g7pt`**. Side effect:
   .text +8 B (two `mask` accesses lost gp relaxation), pimout moved 4.7 KB ⇒ O3
   re-measured on g7pt: 208,743 → 204,733 (−1.9%, inside ±3%).
3. **Minor has no functional unit for segmented vector loads/stores.** gem5 v25's
   `MinorDefaultMemFU` omits the five `Simd*Segmented*` classes (the startup
   `No functional unit for OpClass` warnings); such an instruction never issues and
   the core stalls silently. OpenBLAS's sgemm path has 386 (`sgemm_oncopy` =
   `vsseg8e32.v`), reached by `warmup()` and `attention()`. ⚠️ The 2026-09-16 note
   that the Minor baseline "runs fine" was wrong: DRAM traffic stopped at clk
   245,398,233, only refreshes followed, stats.txt stayed empty. **Fix:** a Minor pool
   with those classes in `fs_bert.py` + `se_bert.py`. PIM binaries contain none ⇒ the
   Minor PIM rerun with the fix is simTicks-identical.

## Verification — everything checked, all passed

- **Machine.** config.ini audit of every run: clock 333, L1I/L1D 16 KiB, L2 2 MiB,
  MSHRs, both StridePrefetchers, `pimony_mem.json` + model, log level — identical.
- **Same work.** simInsts identical per block on every host (baseline exact; PIM
  g7pt +1 in block 2 from the relaxation change). Baseline L2 demand accesses 226k on
  every host = 14 MB of FP16 weights once; PIM side 2k.
- **PIM did the same job everywhere.** `quiesceCycles` per matmul block is
  host-invariant: 92.8k Minor / 93.9k O3 / 95.0k matched O3. `inst.quiesce` = 8 in
  every PIM run (3/1/1/3). Fences: CleanShared 680 / Invalidate 264 on timing and
  Minor (O3 673/262, MSHR merges). Paging free: 1 dtb miss, 0 walks in the ROI.
- **Arithmetic.** The method reproduces 8.43x and 15.30x exactly on the old runs;
  a simSeconds cross-check agrees.
- **Branches agree across hosts** (attention and GELU instruction counts identical),
  so the data flowing through is consistent. Checksums were never captured.

## Layout sweep — the reason the answer is a range

```
PIM matmul            default      a16        a32        (-falign-functions)
Minor                 374,130    373,287    375,015      16.54x / 16.58x / 16.50x
O3, Minor's VFU       206,714    199,951    195,367      17.53x / 18.12x / 18.55x
out-of-order gain         6%         9%        12%
fetch/inst  Minor  .236 .216 .215     O3  .115 .082 .053
```

Minor is layout-stable; matched O3 is not (busy time ~linear in fetch/inst). a16 and
a32 fetch almost identically on Minor, as `reference-gem5-fetch-alignment-bias`
warns, so Minor saw two layouts, O3 three. The a32 O3 run exits 30% later — all of it
in init_params before the ROI. Baseline not swept: OpenBLAS kept fetch/inst
1.402/1.408 across builds even on TimingSimple.

## SE vs FS on Minor: +2.71%, and it is layout, not mode

`bert_tax_fs` (fs_bert.py) vs `bert_tax_se` (se_bert.py), identical code: matmul
66,384,194 vs 64,632,979 cycles, instructions identical per block, dcache and L2
identical to 1%. Only icache fetches differ: **+2.76%** (21.86M vs 21.27M), i.e. the
two builds place their hot loops differently. O3 fetches 25% more in FS and loses
0.002%. ⇒ the execution mode costs nothing on any host; Minor is sensitive to code
layout at the ~3% level. (Predicted <0.5% before the run — wrong.)

## GPT-3 decode on Minor — the order flips · 2026-09-30

`gpt3.c` got the same page-table preload → `gpt3_pt`. Matmul blocks 1/3/5/7:

```
                                   baseline        PIM    speedup
Minor                            64,814,111  2,290,760    28.29x
O3 with Minor's vector unit      30,558,375  1,339,410    22.81x   <- the comparison
O3, gem5 default                 25,823,931  1,323,866    19.51x   (published gpt3_dec_b: 19.13x)
```

On BERT out-of-order won by 6%; on GPT-3 in-order wins by 24%. Speedup is
baseline ÷ PIM, and the two sides respond differently to the core:

```
O3 vs Minor, same vector unit       BERT     GPT-3
baseline faster by                 1.71x     2.12x    L2 misses in flight 7.0/2.1  vs  6.8/1.5
PIM run faster by                  1.81x     1.71x    host asleep 46%  vs  72% of the O3 PIM time
```

The baseline streams weights through the 2 MiB L2; out-of-order keeps more misses in
flight (Little's law from misses, average miss latency and time). Minor's misses are
individually cheaper (45 vs 89 cycles on GPT-3) — it is slower because it overlaps
fewer. The PIM run is mostly device time on GPT-3, which no host core changes; O3
speeds the remaining host work up 3.56x, but that is 28% of the time.
⇒ the host comparison is workload-dependent; quote it per workload. Checks (all
passed) and runs: `gpt3/GAPS_gpt3.md` top block.

## Still open

- A fully matched FP pool on O3, which would tighten the 6–12% (downward).
- Minor's memory parallelism is gem5's default Minor LSQ, not a measured in-order
  core; GPT-3's flip depends on it.
- ROB/LQ sweep on O3 (the xsgem5-rejection plan) — the host space is still two points.

## Binaries and runs

| binary | build | runs |
|---|---|---|
| `bert_pim_g7pt` | `bert_pim_g7` recipe; page table preloaded | `m5out/bert_pim_g7pt_{minor,minor_rerun,o3,o3v61}` |
| `bert_pim_g7pt_a16/a32` | + `-falign-functions=16/32` | `m5out/bert_pim_g7pt_a{16,32}_{minor,o3v61}` |
| `bert_blas_fair` | unchanged | `m5out/bert_blas_fair_{minor,o3_fix,o3v61}` |
| `bert_tax_fs` / `_se` | unchanged | `m5out/tax_{fs,se}_minor` |

Run lines: `fs_bert.py minor configs/scratch/progs/bert_pim_g7pt`,
`se_bert.py minor configs/scratch/progs/bert_blas_fair`; matched O3 = append `6 1`.

# G13 — O3 `pim.fence` RELEASED ITS BARRIER AT EXECUTE · CLOSED 2026-10-02

## What was wrong

`pim.fence.cl/inv/flush` are CBMOp stores carrying IsReadBarrier + IsWriteBarrier. O3's
dispatch (`iew.cc`) tests `isStore()` before the barrier case, so the fence took the store
path. A store frees its MemDepUnit barrier at IEW writeback, i.e. when it executes
(`iew.cc:1407` → `IQ::wakeDependents` → `MemDepUnit::completeInst`), but its clean or
invalidate leaves the store queue only after commit. Later loads — and the next fence —
ran before the cache operation. Minor frees its barrier only when the fence's store-buffer
entry completes, so Minor was always right.

Evidence before the fix, `fence_cbo` (`m5out/ver_fence_cbo_*`): DRAM accesses to the
`pim.fence.inv` line / `pim.fence.flush` line were 2 / 3 on TimingSimple and Minor, **1 / 2
on O3** — the reload after the fence never reached DRAM, it read the old cached copy.
O3 also merged fences into in-flight MSHRs (G8.1's "7.6x overlap"); Minor never did.

## The fix (`ed427c0`, O3 only, 3 files)

A store that is also a barrier skips the MemDepUnit completion at writeback;
`LSQUnit::completeStore` releases it when the cache operation's response arrives
(`InstructionQueue::completeStoreBarrier`). `pim.fence` is the only RISC-V store that is a
barrier (`fence` and the AMO fence micro-ops are non-memory barriers, untouched).

## Verified

- `fence_cbo` on O3: 2 / 3 DRAM accesses, as on TimingSimple and Minor (`m5out/fix2_fence_cbo_o3`).
- `fence_order` on O3: with the fence, load B is sent at 162 ns after A completes at
  130 ns; without it, B is sent at 71 ns (overlap). The demo finally shows the barrier.
- All 9 tests pass on all three CPUs; TimingSimple and Minor tick- and trace-identical to the
  previous build; O3 device command streams identical to TimingSimple's.
- BERT / GPT-3 O3 reruns (`m5out/*_o3*_f2`): config.ini identical to the old runs,
  instructions identical, PIM sleep −0.5% to +3.2%. Numbers in the top block.

## Consequence

The O3 host comparison of G12 is superseded (top block). G8.1's case against a range fence
was built on O3 overlapping its fences; with real fences every line pays its own round
trip on both cores, so the range fence should be re-measured (done 2026-10-03: 22.7%, see
G8.1). G9 and the GPT-3 overlap /
interference results ran on the broken O3; rerun 2026-10-03, see the top block.

# G14 — O3 LEAKED THE TOKEN OF A SQUASHED OFFLOAD · CLOSED 2026-10-02

O3 executes `pim.dispatch` / `pim.gemv` (and allocates the token) before commit; a squash
before commit discarded the instruction with its token still marked in use, and only
`pim.wait` frees tokens. Seen on `sweep` (`m5out/leak_sweep_o3`): a memory-order violation
squashed the second dispatch (sn:1206) after it took token 1; its refetch took token 2.
64 such leaks would hang `pim.wait`. The July fix (token kept on the instruction) covered
a replay of the same instruction, not a squash. Fixed in `c01ad8a`: `LSQUnit::squash`
returns the token through a new `BaseISA::freePimToken` hook when the load never reached
commit. All 9 tests tick- and trace-identical before/after; allocation is round-robin, so
token numbers and results are unchanged.

