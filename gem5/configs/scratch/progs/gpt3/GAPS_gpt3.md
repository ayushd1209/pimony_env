# GPT3-2.7B decode port, fairness audit, and the first overlap result

Opened 2026-09-21. Everything here is in `configs/scratch/progs/gpt3/`.
**The BERT work is untouched** — `bert.c`, `bert_blas.c`, all `m5out/bert_*`
and `m5out/blas_*` runs are exactly as they were. This is a parallel tree.

Status: `CLOSED` · `OPEN`

---

## THE NUMBERS — 2026-09-24, o3, cycles from simTicks (3 GHz, /333.33)

**Quote matmul-only.** Blocks 1/3/5/7. See "whole-layer is unsound" below —
that is not the old G3 style-of-caution, it is a hard result.

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

## G-OV — THE FIRST OVERLAP RESULT · NET NEGATIVE, mechanism CONFIRMED

`pim.gemv` returns immediately and `pim.wait` blocks separately, precisely so the
host can work in between. Nothing had ever used it: `pim_linear` was
issue → wait → read back, per slab.

**What makes it possible:** one matmul is not one PIM job. A `pim.gemv` reduces
over at most 1024 inputs (a 2 KB DRAM row at FP16, and a MAC cannot cross a row).
`in_dim` 2560 ⇒ 3 slabs, 10240 ⇒ 10. **25 jobs and 25 windows per layer.**

**The rule:** work may move into a window only if it does not depend on the job
being waited on. Reading back slab p-1 qualifies.

```c
for (p = 0; p < parts; p++) {
    tok = pim_issue(...);                       /* PIM starts on slab p   */
    if (p > 0) pim_consume(p - 1, b, y, ...);   /* host reads slab p-1    */
    pim_wait(tok);                              /* whichever is longer    */
}
pim_consume(parts - 1, ...);                    /* no window left         */
```

`-DPIM_OVERLAP` in `gpt3.c`. The control (`#else`) uses the SAME `pim_consume`,
so the A/B isolates *where* the read-back happens and nothing else.

### RESULT

```
matmul sections, o3      cycles      busy     asleep      insts
  original  (dec_b)   1,349,774   392,056    957,718   1,050,214
  control   (dec_c)   1,658,066   695,765    962,301   1,294,857
  OVERLAP   (dec_ov)  1,405,532   720,857    684,675   1,295,464
```

**The mechanism works.** Control vs overlap: instructions identical (+0.05%),
sleep down **277,626 cycles = 28.9%**. It is genuinely hiding host work inside
PIM's, not doing less work.

**The restructure it required cost more than it saved.** Slab-major read-back
added **244,643 instructions (+23%)** and nearly doubled host busy time
(392,056 → 695,765). Overlap returned 252,534; the restructure had taken 308,292.

```
  19.13x  ->  18.37x       net, against the original
```

### THE CAUSE, AND IT IS SPECIFIC

```c
/* original: y written ONCE per output */
for (o...) { acc = b[o]; for (p...) acc += slab[p][o]; y[o] = acc; }

/* slab-major: y read-modify-written ONCE PER SLAB */
for (p...) for (o...) y[o] += slab[p][o];
```

W2: 10 slabs x 2,560 outputs ⇒ `y` loaded and stored **25,600** times instead of
2,560. L2 misses barely move (2,753 → 2,990), so it is instruction count and L1
traffic, not misses.

Slab-major is forced by overlap — you must consume one slab at a time.

### OPEN — left deliberately, revisit later

1. **Batch the consume.** Take 2 or 4 slabs per pass instead of 1: load `y` once,
   add several slabs, store once. W2 still gets 5 windows at pairs. Halves or
   quarters the `y` traffic while keeping most of the overlap. **Untried.**
2. **Is it worth it at all?** If the batched version cannot get below the
   original's 392,056 busy cycles, the honest answer may be that the read-back
   is already cheap enough and the sleep must be filled from elsewhere.
3. **Attention scores during Wv.** Unbuilt, and the larger prize. Scores need Q
   and K, not V, so they are legal inside Wv's three windows. Attention is
   2,098,320 cycles against 957,718 of total sleep — supply is not the problem,
   dependency is.
4. ⚠️ **`fetch/inst` moved** across the three builds: 0.099 / 0.129 / 0.139. Per
   G5 that is a fairness flag. Absolute values are tiny (excellent icache
   behaviour either way) and the control-vs-overlap instruction counts are
   identical, so the 28.9% sleep reclaim stands — but a rebuild-both-sides
   layout check has NOT been done.

### What blocks 3, 5 and 7 cannot borrow

`Wo` needs `ctx`, `W1` needs `norm1`, `W2` needs `hid` — each produced by the
code immediately before it. They can only overlap with their own slabs. That is
enough in principle: W2's ten slabs are ten windows for nine read-backs.

---

## BINARIES AND RUNS

| binary | build | run |
|---|---|---|
| `gpt3_pim` | GPT-3 dims, BERT attention (step 1 only) | `m5out/gpt3_pim_o3` |
| `gpt3_dec` | + KV cache, decode attention, pre-LN | `m5out/gpt3_dec_o3` |
| `gpt3_dec_b` | + BLAS attention kernels | `m5out/gpt3_dec_b_o3` |
| `gpt3_dec_c` | + slab-major read-back (overlap CONTROL) | `m5out/gpt3_dec_c_o3` |
| `gpt3_dec_ov` | + `-DPIM_OVERLAP` | `m5out/gpt3_dec_ov_o3` |
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
