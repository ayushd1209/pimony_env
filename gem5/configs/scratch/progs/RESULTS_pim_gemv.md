# pim.gemv on a BERT layer — measured result (SEQ=1)

Measured 2026-09-15. Companion to `RESULTS_gemv.md`, which is the frozen
**CPU-only** baseline study from 2026-08-20 and whose machine block is stale.
This file is the first end-to-end PIM result.

## What is compared

One BERT-base encoder layer, batch 1. **One variable: how the six matmuls are
done.** Everything else — attention, GELU, LayerNorm, residuals — runs on the
CPU in both.

| | matmuls | binary | config |
|---|---|---|---|
| baseline | OpenBLAS `shgemv`, FP16, vectorised | `bert_blas_s1_fp16` | `se_bert.py timing` |
| PIM | 8 × `pim.gemv` | `bert_pim` | `fs_bert.py timing` |

## Machine — verified identical, from each run's `config.ini`

Check the run, not the config script. `m5out/*/config.ini` is what was actually
instantiated.

`BaseTimingSimpleCPU` @ 3 GHz (clock=333 ps). L1I/L1D 16 KiB 2-way (L1D +
StridePrefetcher, mshrs 16). L2 2 MiB 8-way, mshrs 20, + StridePrefetcher.
PIMony/DRAMsim3, LPDDR5X-8533, 4 channels. 2 GB at 0x80000000.

`diff` of `se_bert.py` vs `fs_bert.py` shows they differ **only** in SE-vs-FS
workload setup and the PIM MMIO/interrupt wiring. SE-vs-FS mode tax was
separately calibrated at 0.36% on identical code (2026-08-25).

## Result

ROI = one `bert_layer()` call. Weight layout excluded (one-time model-load cost);
`pim_store_vec` **included** — it is per-matmul host work on the critical path.

| | baseline | pim.gemv | ratio |
|---|---|---|---|
| cycles | 8,932,476 | **1,565,985** | **5.70x** |
| time | 2.975 ms | **0.521 ms** | 5.71x |
| instructions | 1,872,593 | 495,969 | 3.78x |
| CPI | 4.77 | 3.16 | |
| L2 demand misses | 54,856 | 1,021 | 53.7x |

L2 demand misses are the mechanism: the weights no longer cross the memory bus
to reach the core. (Demand misses only — prefetches are counted separately, so
this is not total DRAM traffic.)

> **If this is ever repeated on `minor` or `o3`, compare `simSeconds`, not
> `numCycles`.** `numCycles` on TimingSimpleCPU is whole runtime; on minor and o3
> it counts *active* cycles only, so the time the core sleeps in `pim_wait` would
> vanish and PIM would look artificially good. See `docs/perf_analysis_notes.md`
> 8.3. Here the two agree — 1,565,985 x 333 ps = 521.47e6 ticks = ROI `simTicks`
> — which is what confirms the PIM's compute time is inside the number.

## Where the time goes — and why this matters more than the headline

| | cycles | share |
|---|---|---|
| CPU busy | 1,478,423 | **94.4%** |
| CPU idle (= PIM compute) | 87,561 | **5.6%** |

Bare metal, one thread, and the only thing that suspends it is `pim_wait`, so
idle cycles are PIM compute time. The baseline's `numIdleCycles` is 0, which
confirms the reading.

Cross-check on the 5.6%: 87,561 / 8 jobs ~ 10,900 cycles ~ 3.6 us each.
Independently, 110,592 steps / 32 engines x tCCD_L 4 ~ 13,800 DRAM cycles per
layer — same order, the gap being row activates and READRES.

**Consequence.** Over 99% of the arithmetic was moved off the core and now costs
5.6% of the runtime. **An infinitely fast PIM would take 5.70x only to 6.04x.**
The bottleneck has moved to the 94.4% that is still CPU work: attention, GELU,
LayerNorm, residuals, and `pim_store_vec`. Further PIM-side optimisation, and
overlapping host work with PIM work, are each worth at most ~6%.

## Validation — what proves the offload actually happened

Every figure below was **predicted before the run** and matched exactly.

| stat | predicted | measured |
|---|---|---|
| `num_comp_cmds` | 110,592 | 110,592 |
| `num_gwrite_cmds` | 32 | 32 |
| `num_readres_cmds` / `_done` | 8,448 | 8,448 / 8,448 |
| `system.workload.inst.quiesce` | 8 | 8 |

`num_comp_cmds` = 110,592 derived two independent ways:
- per matmul: `4x(192x48) + 768x48 + 3x(192x64)`
- from the weights: 7,077,888 elements / 64 per step (4 banks x 16 FP16)

An exact match means every weight element was touched exactly once — no dot
dropped, none double-counted — that D2GWRITE fired once per channel per
instruction (8 x 4), and that all 8,448 accumulator readouts drained.

`inst.quiesce` counts **actual sleeps** (`System::Threads::Thread::quiesce`), so
8 means each `pim.gemv` launched work still running when the CPU reached its
wait, and each was woken by its own completion. Fewer would mean a job finished
instantly (no real work); more would mean a spurious wake.

There is **no MAC command counter** in PIMony — use `num_comp_cmds`. It is
incremented by `cmd.num_macs` per MAC and only when `is_first_comps`, which
originals carry and resumes do not, so it is immune to preemption resumes.

## Known limits — stated deliberately

1. **PIMony computes no values.** Results are never read back, so the cost of
   consuming them is not modelled and the bias add is a bare store.
2. **W2's three partial sums are never added** (~2 x 768 adds plus traffic).
3. **The checksum is meaningless in this build** — downstream activations run on
   uninitialised memory. The correctness anchor is the command count above, which
   verifies the sequencer rather than the CPU's float arithmetic.
4. **SEQ=1.** This is the decode-shaped, batch-1 case the workload partition
   deliberately targets. Arithmetic intensity rises ~71x from SEQ 1 to 128 and
   PIM's case is gone by SEQ~64 — so quote this as *batch-1*, never as "BERT".
5. **The CPU idles through every `pim.gemv`**, so 5.70x is a floor — but see the
   5.6% above for how small that floor effect is.

## Reproduce

```sh
# binary (host toolchain; not in the container)
cd gem5/configs/scratch/progs
riscv64-unknown-elf-gcc -O3 -march=rv64gcv -mabi=lp64d -mcmodel=medany \
  -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math \
  -DFREESTANDING -DNO_IO -DBAREMETAL -DPIM_LAYOUT -DPIM_GEMV \
  -nostdlib -nostartfiles -T bert_bm.ld -o bert_pim bert.c -lgcc

# run (container only)
build/RISCV/gem5.opt -d m5out/bert_pim_v3 \
    configs/pimony/fs_bert.py timing configs/scratch/progs/bert_pim
cp dramsim3.txt m5out/bert_pim_v3/dramsim3_final.txt   # see trap below

# counts
grep -E "num_comp_cmds|num_gwrite_cmds|num_readres_cmds" \
  m5out/bert_pim_v3/dramsim3_final.txt \
  | awk '{s[$1]+=$3} END {for (k in s) print k, s[k]}'
```

**Trap:** `dramsim3.{txt,json,epoch.json}` are written to the **gem5 root**, not
the output directory, and are overwritten by the next run — copy them
immediately. They are also written only on the **exit callback**, while a gem5
stats *reset* wipes PIMony's counters — so the ROI must end with
`m5_dump_stats()` (M5OP_DUMP_STATS, `.word 0x8200007B`), not
`m5_dump_reset_stats()`. With dump-reset, every PIM counter reads zero.

Stats dirs: `m5out/bert_pim_v3` (PIM), `m5out/blas_s1_fp16` (baseline).
