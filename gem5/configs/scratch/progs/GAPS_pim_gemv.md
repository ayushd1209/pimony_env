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

## THE NUMBERS — 2026-09-19

```
 8.24x   PIM vs OpenBLAS, MATMUL SECTIONS ONLY (blocks 1,3,5,7)   <- THE HEADLINE
 7.46x   PIM vs OpenBLAS, whole layer
21.8x    OpenBLAS vs the hand-written kernel  (= how good the baseline is)
```

Cycles behind them: OpenBLAS matmul **7,205,968** / layer **7,844,458**;
PIM (`bert_pim_g7`) matmul **874,932** / layer **1,051,688**.

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

Remaining are scope statements, not defects: SEQ=1 only (quote as *batch-1*,
never "BERT"); PIMony produces no values. Future work, none blocking: the
host config sweep on o3/minor (compare `simSeconds`, NEVER `numCycles`), the SEQ
crossover on the real instruction, and the fence-vs-uncacheable A/B (G2 option 3).

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

## Also stale

`RESULTS_pim_gemv.md` carries 8,448 in three places (`:20`, `:111`, `:120`).
Not corrected here — separate pass.

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
