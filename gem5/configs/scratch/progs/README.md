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
| `bert_pim_g7pt` | **the PIM build** — the six matmuls become 8 `pim.gemv`, results read back, page table preloaded in the image (needed on Minor, GAPS G12). See below |
| `bert_pim_g7pt_a16`, `_a32` | same, `-falign-functions=16/32`: the layout sweep |
| `bert_pim_g7` | the previous PIM build (page table stored at run time): source of the 2026-09-19 timing/O3 numbers; crashes on Minor |
| `bert_pim_g7_nf` | `bert_pim_g7` + `-DNO_PIM_FENCE`: the fence A/B control |
| `bert_cpu` | `-DPIM_FP16`: same file, PIM off — the same-source baseline |
| `bert_disp` | `-DPIM_GEMV -DPIM_BY_DISPATCH`: the same matmuls as 2,112 `pim.dispatch`, 1 at a time then wait (GAPS G9) |
| `bert_disp_w32`, `_w192` | same + `-DPIM_WAVE=32` / `=192`: 32 then wait (runs, the schedule G9 quotes) / all at once (PIMony aborts). Built from commit `7066456`'s `bert.c`, whose plain build is byte-identical to `bert_disp` — see below |

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
  -nostdlib -nostartfiles -T bert_bm.ld -o bert_pim_g7pt bert.c -lgcc

build/RISCV/gem5.opt -d m5out/bert_pim_g7pt_minor configs/pimony/fs_bert.py \
    minor configs/scratch/progs/bert_pim_g7pt
build/RISCV/gem5.opt -d m5out/bert_pim_g7pt_o3v61 configs/pimony/fs_bert.py \
    o3 configs/scratch/progs/bert_pim_g7pt 6 1      # O3 with Minor's vector unit
```

`6 1` = vector opLat 6, 1 unit (argv[3]/[4]); it applies to `o3` only and is what
makes Minor vs O3 a comparison of issue order rather than vector width.

`-DPHASE_STATS` dumps and resets stats at 8 points in `bert_layer()`, so blocks
1/3/5/7 are the matmuls and 2/4/6/8 the control code. Every published figure is
sectioned this way; a run without it cannot be compared to one.

Two build switches exist for A/Bs:

| flag | what it does | why |
|---|---|---|
| `-DPIM_FP16` | same file, PIM **off**, FP16 CPU kernels | the same-source baseline |
| `-DNO_PIM_FENCE` | drops the fence instructions, **keeps** the `"memory"` clobber | fence A/B. Deleting the wrapper instead would also delete the result read-back, which only survives because of that clobber |

⚠️ **Run on `minor` and `o3`, not `timing`.** Matmul-only speedup after the O3 fence
fix (2026-10-02, GAPS G13): **BERT 16.54x on Minor, 12.59x on default O3; GPT-3 decode
28.29x on Minor, 16.24x on default O3** — the in-order core shows the larger speedup on
both, while O3 + PIM is fastest in absolute time. TimingSimpleCPU (8.43x on BERT) blocks
on every memory access and understates PIM ~2x. The earlier O3 figures (15.60x / 17.53x,
22.81x, "out-of-order adds 6–12%") came from the fence bug and are superseded.

Result and validation: **`RESULTS_pim_gemv.md`**, and **`GAPS_pim_gemv.md`** for
the full record — read its top NUMBERS block before quoting anything.

The `pim.dispatch` binaries come from the `bert.c` of commit `7066456` (the current file
adds only the page-table preload, which changes the image). Build them outside the tree
so the source keeps its name:

```sh
P=~/pimony_env/gem5/configs/scratch/progs
mkdir -p /tmp/disp_src && cd /tmp/disp_src && git -C $P show 7066456:./bert.c > bert.c
B="-O3 -march=rv64gcv_zfh -mabi=lp64d -mcmodel=medany -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math -DFREESTANDING -DNO_IO -DBAREMETAL -DPIM_LAYOUT -DPIM_GEMV -DPHASE_STATS -DPIM_BY_DISPATCH -nostdlib -nostartfiles -T $P/bert_bm.ld"
riscv64-unknown-elf-gcc $B -o check bert.c -lgcc && cmp check $P/bert_disp && echo IDENTICAL
riscv64-unknown-elf-gcc $B -DPIM_WAVE=32 -o $P/bert_disp_w32 bert.c -lgcc
```

Helper scripts here, for runs whose log keeps the PIMony command lines:
`engine_gaps.py <run.log>` (does any MAC reach a still-busy engine?) and
`writeback_wait.py <run.log>` (how long the sequencer's result write-backs wait).

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
| `bert_blas_s128_oct` … `_s512_oct` | 128, 256, 384, 512 — built 2026-10-03 against the September OpenBLAS install; `_s128_oct` reproduces `bert_blas_s128` within 0.012% (runs `m5out/l2big-s*_oct`) |

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
