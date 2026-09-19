# What's in here

Two families of BERT-layer binary. They exist for different jobs and are built
from different sources — don't mix them up.

## bert.c — bare metal, for the PIM runs

No libc. Hand-written maths (`k_expf`, `k_tanhf`), raw `ecall` for output,
static arrays instead of `malloc`. Runs under `fs_*.py` in full-system mode,
which is the only place `pim.dispatch` / `pim.wait` work.

| binary | build |
|---|---|
| `bert_bm` | vector, `-march=rv64gcv` |
| `bert_bm_scalar` | scalar, `-march=rv64gc` |
| `bert_rvv`, `bert_rvv_fast` | earlier vector attempts, superseded by `bert_bm` |
| `bert_bm_s8`, `bert_bm_s8_scalar` | int8 variants, unmeasured so far |
| `bert` | earliest version, kept for reference |
| `bert_se` | SE-mode twin of `bert_bm`, for the SE-vs-FS calibration |
| `bert_pim_g7` | **the PIM build** — the six matmuls become 8 `pim.gemv`, results read back. See below |
| `bert_pim_g7_nf` | same, `-DNO_PIM_FENCE`: the fence A/B control |
| `bert_cpu` | `-DPIM_FP16`: same file, PIM off — the same-source baseline |

```sh
riscv64-unknown-elf-gcc -O3 -march=rv64gcv -mabi=lp64d -mcmodel=medany \
  -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math \
  -DFREESTANDING -DNO_IO -DBAREMETAL \
  -nostdlib -nostartfiles -T bert_bm.ld -o bert_bm bert.c -lgcc
```

`bert_se` is the same source built for SE mode — drop `-DNO_IO -DBAREMETAL`
and the linker script, keep every other flag so the codegen matches:

```sh
riscv64-unknown-elf-gcc -O3 -march=rv64gcv -mabi=lp64d -mcmodel=medany \
  -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math \
  -DFREESTANDING -nostdlib -nostartfiles -o bert_se bert.c -lgcc
```

The RWX-segment linker warning is benign. Verify the builds match before
trusting a comparison — vector ops per function must be equal:

```sh
riscv64-unknown-elf-objdump -d <bin> | awk '
  /^[0-9a-f]+ <.*>:/ { fn=$2; gsub(/[<>:]/,"",fn) }
  /vfmacc|vsetvli/ { c[fn]++ }
  END { for (f in c) printf "  %-24s %d\n", f, c[f] }' | sort
```

Expect `main 36` + `layernorm 8` in both; `bert_se` has 4 extra in `put_num`,
which runs after the ROI closes. Measured 2026-08-25: identical simInsts
(8,125,528), cycles within 0.36%.

`-mcmodel=medany` is required — at 0x80000000 the default can't reach `.bss`.
Host toolchain only; `riscv64-unknown-elf-gcc` is not in the container.

## bert_pim — the PIM build (`-DPIM_GEMV`)

Same source, two more flags. `-DPIM_GEMV` replaces the six `linear()` calls with
8 `pim.gemv` offloads and pulls in the completion scaffolding (trap handler,
Sv39 paging, M->S drop in `_start`). It **requires** `-DPIM_LAYOUT`, which is
enforced by an `#error` — the offload reads weights in PIM order.

⚠️ **`zfh` is not optional.** Without it every `float -> _Float16` cast becomes a
174-instruction libgcc call inside the ROI (`__truncsfhf2`), and you measure
libgcc rather than the offload. It was worth 2x on this binary. See GAPS G1.

```sh
riscv64-unknown-elf-gcc -O3 -march=rv64gcv_zfh -mabi=lp64d -mcmodel=medany \
  -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math \
  -DFREESTANDING -DNO_IO -DBAREMETAL -DPIM_LAYOUT -DPIM_GEMV -DPHASE_STATS \
  -nostdlib -nostartfiles -T bert_bm.ld -o bert_pim_g7 bert.c -lgcc

build/RISCV/gem5.opt -d m5out/bert_pim_g7_o3 configs/pimony/fs_bert.py \
    o3 configs/scratch/progs/bert_pim_g7
```

`-DPHASE_STATS` dumps and resets stats at 8 points in `bert_layer()`, so blocks
1/3/5/7 are the matmuls and 2/4/6/8 the control code. Every published figure is
sectioned this way; a run without it cannot be compared to one.

Two build switches exist for A/Bs:

| flag | what it does | why |
|---|---|---|
| `-DPIM_FP16` | same file, PIM **off**, FP16 CPU kernels | the same-source baseline |
| `-DNO_PIM_FENCE` | drops the fence instructions, **keeps** the `"memory"` clobber | fence A/B. Deleting the wrapper instead would also delete the result read-back, which only survives because of that clobber |

⚠️ **Run on `o3`, not just `timing`.** The speedup is 8.24x on TimingSimpleCPU and
15.28x on O3 — the host model is not a detail, it is the dominant variable, and
TimingSimpleCPU blocks on every memory access so nothing overlaps there. GAPS G8.

Result and validation: **`RESULTS_pim_gemv.md`**, and **`GAPS_pim_gemv.md`** for
the full record — read its top NUMBERS block before quoting anything.

Check the weight bases are 256 KB aligned before trusting a run — the sequencer
refuses anything finer:

```sh
riscv64-unknown-elf-nm bert_pim | grep -E ' (Wq|Wk|Wv|Wo|W1|W2|pimvec)$'
```

Every address must be a multiple of `0x40000`.

`bert_bm.ld` carries a `PHDRS` directive that exists only for this build: the
256 KB `PIM_ALIGN` otherwise makes `ld` give `.bss` its own `PT_LOAD` at a file
offset past the end of the file, and gem5 bounds-checks that offset before
noticing the segment has no file bytes — "Segment outside the bounds of the
image data". One segment fixes it; the file grows to ~277 KB of padding.

## bert_blas.c — hosted, the host baseline

Every matmul goes through OpenBLAS. Softmax, GELU and LayerNorm stay
hand-written because BLAS has no routine for them — same split PyTorch uses.
Needs libc, so it only runs under `se_bert.py` in syscall-emulation mode.
**No PIM here**: SE mode has no MMIO and no interrupts.

| binary | SEQ |
|---|---|
| `bert_blas` | 1 |
| `bert_blas_s4` | 4 |
| `bert_blas_s16` | 16 |
| `bert_blas_s64` | 64 |
| `bert_blas_s128` | 128 |

```sh
docker exec -w /home/pimony/gem5/configs/scratch/progs gem5-container \
  /opt/riscv/bin/riscv64-unknown-linux-gnu-gcc -O3 -march=rv64gcv -mabi=lp64d \
    -static -DSEQ=16 \
    -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math \
    -I/opt/openblas-zvl256b/include \
    -o bert_blas_s16 bert_blas.c /opt/openblas-zvl256b/lib/libopenblas.a -lm
```

GCC 16 is required. GCC 13 and earlier compile OpenBLAS's vector kernels to
*scalar* code silently — the build succeeds and functional tests pass. Check with:

```sh
riscv64-unknown-elf-objdump -d <binary> | grep -c "vfmacc\|vsetvli"
```

Expect thousands. Near zero means it fell back.

## bert_se_blas — keep this one

Built against OpenBLAS's **scalar reference** target (`RISCV64_GENERIC`), which
has since been deleted. **Not rebuildable.** It's the only surviving artifact of
the experiment showing that an untuned library kernel is no faster than a
hand-written loop — 37.30M cycles for both.

## Not BERT

`add16_test`, `addtest`, `matmul` — old standalone tests, unrelated.
