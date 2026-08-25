# BERT layer baseline — GEMV case (SEQ=1)

Frozen 2026-08-20. Do not overwrite; the GEMM work uses separate binaries.

## Workload
bert.c, HIDDEN=768 HEADS=12 FFN=3072 **SEQ=1** ITERS=1
7.08 M multiply-accumulates, 7.1 M weights (28.3 MB fp32), each weight read once.
SEQ=1 makes this matrix-VECTOR (GEMV) — no weight reuse. Represents
autoregressive decode, NOT how BERT is normally run (which is GEMM).

## Machine (configs/pimony/fs_bert.py — same as fs_fence_e2e.py)
RiscvO3CPU @ 1GHz, 8-wide.  L1I/L1D 16KiB 2-way, L2 256KiB 8-way.
Memory: PIMony / DRAMsim3, LPDDR5X-8533, 4 channels (~136 GB/s peak).
Bare metal (RiscvBareMetal), no OS.  VLEN=256, ELEN=64.

> **Stale as of 2026-08-25 — do not compare these cycles against a fresh run.**
> fs_bert.py/se_bert.py have since moved to **3 GHz** and a **2 MiB L2**. DRAM
> latency is fixed in ns, so 3x the clock triples its cycle cost: the same
> bert_bm now measures 8.10M cycles (IPC ~1.0) instead of 4.75M, yet is *faster*
> in time (2.70 ms vs 4.75 ms). Only the within-table ratios below survive.

## Results (ROI = one bert_layer() call, weight init excluded)

| run | binary | mshrs | prefetch | vec FMA lat | instrs | cycles | time | IPC |
|-----|--------|-------|----------|-------------|--------|--------|------|-----|
| untuned baseline | bert_bm        | 4  | none   | 1 | 8,125,528  | 37,692,436 | 37.69 ms | 0.216 |
| tuned, vector    | bert_bm        | 16 | stride | 1 |  8,125,528 |  4,746,636 |  4.75 ms | 1.712 |
| tuned, vector, fair FMA | bert_bm | 16 | stride | 5 |  8,125,528 |  5,887,055 |  5.89 ms | 1.380 |
| tuned, scalar    | bert_bm_scalar | 16 | stride | - | 42,709,633 | 35,800,248 | 35.80 ms | 1.193 |

## Headline numbers
- Cache tuning (mshrs 4->16 + stride prefetcher): **7.9x**  (37.69 -> 4.75 ms)
- Vectors, fair FMA latency:                      **6.1x**  (35.80 -> 5.89 ms)
- Vectors, gem5 default FMA latency:              7.5x  -- inflated, do not quote

## Why each number is what it is
- Scalar: 7.08M MACs x 5-cycle FloatMultAcc = 35.4M cycles vs 35.8M measured.
  Bound by the SERIAL fp accumulate chain, not memory (dcache miss 1.0%).
  This is a compiler artifact -- a blocked/multi-accumulator kernel would be
  faster, so 6.1x answers "-march=rv64gcv vs rv64gc at -O3", nothing more.
- gem5 charges scalar FloatMultAcc 5 cycles but SimdFloatMultAcc 1 cycle
  (FuncUnitConfig.py:72 vs :102 -- the Simd entries have no opLat at all).
  Setting vector FMA to 5 costs 1.24x; that gap is a model artifact.
- Untuned run was limited by mshrs=4: 4 outstanding misses x 64 B / ~100 ns
  caps bandwidth near 2.5 GB/s regardless of DRAM capability.
- NOT memory-bandwidth-bound in any run: 28.3 MB / 5.89 ms = 4.8 GB/s of the
  ~136 GB/s available (<5%). PIM's case here must rest on overlap and latency,
  not on the CPU exhausting bandwidth.

## Correctness
SE-mode run of the same source matched the x86 host to 4-5 significant figures
(checksum 75.5259 vs 75.5525; difference is RISC-V fused multiply-add using one
rounding step where x86 uses two). Bare-metal instruction count 8,125,528 vs
SE-mode 8,118,628 (0.08%) confirms the port runs identical work.

## Reproduce
    riscv64-unknown-elf-gcc -O3 -march=rv64gcv -mabi=lp64d -mcmodel=medany \
      -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math \
      -DFREESTANDING -DNO_IO -DBAREMETAL \
      -nostdlib -nostartfiles -T bert_bm.ld -o bert_bm bert.c -lgcc
    # scalar: swap -march=rv64gcv for -march=rv64gc

    build/RISCV/gem5.opt --outdir=m5out/X configs/pimony/fs_bert.py \
        o3 <binary> [vec_fma_latency] [vec_units]

Stats dirs: m5out/bert-bm, bert-bm-pf, bert-bm-lat5, bert-bm-scalar
