# GPT3-2.7B decode port, fairness audit, and the first overlap result

Opened 2026-09-21. Everything here is in `configs/scratch/progs/gpt3/`.
**The BERT work is untouched** — `bert.c`, `bert_blas.c`, all `m5out/bert_*`
and `m5out/blas_*` runs are exactly as they were. This is a parallel tree.

Status: `CLOSED` · `OPEN`

---

## THE NUMBERS — 2026-09-24, o3, cycles from simTicks (3 GHz, /333.33)

**THE HEADLINE RESULT OF THE OVERLAP WORK IS NOT A SPEEDUP — see G-INT.** Putting
host work into the PIM wait window costs **+4.7% on every host memory access**,
the first time this platform has had PIM and CPU touching DRAM at once.

**Quote matmul-only.** Blocks 1/3/5/7. See "whole-layer is unsound" below —
that is not the old G3 style-of-caution, it is a hard result.

⚠️ **Everything in this file is O3 only.** GPT-3 has not been run on Minor. On BERT,
in-order Minor vs out-of-order O3 (same vector unit) moved the speedup by only 6–12%
(16.54x vs 17.53x, `../GAPS_pim_gemv.md` G12). **Before a Minor run, port G12's
page-table preload to `gpt3.c`** — its `root_table` is still stored at run time, and
Minor faults on the first fetch after `csrw satp`.

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
experiment.

#### THE POLICY SWEEP THAT WOULD HAVE SEPARATED IT — BLOCKED

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
| `gpt3_att` | + `-DPIM_OVERLAP_ATT` (in flight) | `m5out/gpt3_att_o3` |
| `gpt3_blas` | baseline, `cblas_sgemv` attention | `m5out/gpt3_blas_o3` |
| `gpt3_blas_b` | baseline, raw-kernel attention | `m5out/gpt3_blas_b_o3` |
| `gpt3_blas_c` | + `k_expf`/`k_tanhf`, `-mcmodel=medany` | `m5out/gpt3_blas_c_o3` |

PIM: `-O3 -march=rv64gcv_zfh -mabi=lp64d -mcmodel=medany -DFREESTANDING -DNO_IO
-DBAREMETAL -DPIM_LAYOUT -DPIM_GEMV -DPHASE_STATS -nostdlib -nostartfiles
-T gpt3_bm.ld  gpt3.c sgemv_n.o sgemv_t.o -lgcc`, host toolchain.

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
