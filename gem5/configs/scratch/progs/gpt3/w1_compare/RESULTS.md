# W1 comparison: completed runs

Results checked on 2026-10-27. W1 only: 2560 inputs, 10240 outputs,
FP16 weights. Elapsed cycles are the first complete statistics block's
`simTicks / 333`; PIM sleep is included. Initialization, warmup, scrub and
post-run checks are outside this block.

## Uninterrupted O3 runs (`w1_compare`)

| Vectors | CPU cycles, total | PIM cycles, total | CPU cycles/vector | PIM cycles/vector | CPU/PIM |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 8,414,587 (numerical check FAILED) | 495,371 | excluded | 495,371 | excluded |
| 64 | 108,300,431 | 31,128,759 | 1,692,194.23 | 486,386.86 | 3.4791 |

The CPU-64 numerical check passed. Both PIM programs exited normally after
checking the completion count (3 and 192 jobs). PIMony models timing without
numerical arithmetic, so those checks validate completion, not PIM accuracy.

The 64-vector result directly shows that the current repeated GEMV offload
remains faster than CPU GEMM for this W1 projection. It does not support the
proposed claim that PIM's advantage has disappeared by 64 tokens. It does not
measure a complete prefill layer or locate a crossover.

## Runs with CPU GEMV workaround (`w1_compare_final`)

| Vectors | CPU cycles, total | PIM cycles, total | CPU cycles/vector | PIM cycles/vector | CPU/PIM |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 23,992,398 | 495,514 | 23,992,398 | 495,514 | 48.4192 |
| 64 | 114,012,689 | 31,170,200 | 1,781,448.27 | 487,034.38 | 3.6577 |

Both CPU numerical checks passed (maximum sampled error 0.0000936013894 for
GEMV and zero against the installed GEMM kernel's FP16 accumulation reference).
Both PIM completion checks passed. Reproduce the report with:

```bash
python3 gem5/configs/scratch/progs/gpt3/w1_compare/report.py --tag w1_compare_final
```

These runs execute initialization on AtomicSimpleCPU, then switch to the same
default O3CPU before warmup, scrub and timing. They also link a local OpenBLAS
GEMV override replacing four `vfredusum` instructions with `vfredosum`.
`diagnostics/build_ordered_cpu.sh` verifies every other kernel instruction byte
matches the installed kernel. The installed library and gem5 were not changed.

The original GEMV failed on O3 but passed on TimingSimpleCPU; the ordered
variant passed the O3 probe and full benchmark. This supports an O3 vector
reduction issue; its root cause has not been fixed. The workaround raises the
one-vector CPU time from 8.41 to 23.99 million cycles. Consequently **48.42x is
specific to the workaround and must not be presented as the existing thesis
baseline's speedup**. A validated original-equivalent one-vector timing remains
unavailable.

Compared with uninterrupted O3, the final PIM timings differ by +0.029% at one
vector and +0.133% at 64. CPU-64 differs by +5.274%; the different initialization
and executable state have not been isolated as causes. Use the uninterrupted
O3 64-vector pair for the directly comparable thesis result. Do not merge the
two batches into an unqualified 1-versus-64 speedup curve.

DRAM byte counters in this simulator are cumulative across the reset marker.
They were not used to infer ROI bandwidth or arithmetic intensity here.

Raw outputs are under `gem5/m5out/w1_compare_{cpu,pim}_{1,64}_o3/` and
`gem5/m5out/w1_compare_final_{cpu,pim}_{1,64}_o3/` (`run.log`, `stats.txt`,
`config.ini`). Failed runs are preserved for diagnosis. No additional
simulations were launched during this results check.
