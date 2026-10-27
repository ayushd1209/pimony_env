# W1: CPU reuse versus repeated PIM vector offloads

Completed measurements and validation limits are recorded in [RESULTS.md](RESULTS.md).
It compares one GPT-3 W1 matrix (2560 inputs, 10240 outputs, FP16 weights) applied
to either 1 or 64 input vectors. It does not run a complete prefill layer.

The CPU uses the existing OpenBLAS FP16 GEMV at 1 vector and GEMM at 64. The PIM
benchmark applies the original `gpt3.c` vector-staging, fence, issue, wait and
read-back path to each vector. Each vector needs three reduction-slab jobs, so
the PIM cases complete 3 and 192 jobs. The instruction and sequencer are reused.

Both programs generate the same logical weights, biases and all 64 input vectors
in the same RNG order, regardless of the active count. Both read a 4 MiB scrub
before timing. CPU OpenBLAS warmup is outside timing. Timed work includes CPU
input conversion and bias seeding, or PIM input conversion/staging, fences,
descriptor traffic, sleep, read-back and partial-result accumulation.

The first statistics block is the projection region. The end marker dumps
without resetting, preserving the device counters. Later statistics blocks
include checks and exit and are excluded by the report.

## Build, from the repository root

CPU builds use the hosted toolchain and OpenBLAS inside the existing container:

```bash
docker exec -w /home/pimony/gem5/configs/scratch/progs/gpt3/w1_compare gem5-container bash build_cpu.sh
```

PIM builds use the host's bare-metal toolchain and the existing linker script:

```bash
bash gem5/configs/scratch/progs/gpt3/w1_compare/build_pim.sh
```

These scripts also build small 32-input, 128-output, 4-vector check cases. They
use the same mechanisms without generating the full W1 matrix. The PIM linker
uses section garbage collection so the unused GPT-3 layer and weight arrays
from the included source can be discarded. The binaries have been built and
simulated; both PIM completion checks and the CPU-64 check passed. The original
CPU-1 numerical check failed; see RESULTS.md before using its timing.

## Check the small cases, then run the experiment

```bash
bash gem5/configs/scratch/progs/gpt3/w1_compare/run.sh all check
bash gem5/configs/scratch/progs/gpt3/w1_compare/run.sh all all
python3 gem5/configs/scratch/progs/gpt3/w1_compare/report.py
```

`run.sh` runs sequentially and refuses to overwrite an existing output directory.
It filters simulator output because unfiltered PIMony output can be enormous.
CPU runs use `se_bert.py o3`; PIM runs use `fs_bert.py o3`, as in the thesis setup.
The wrapper checks the simulator's exit cause and CPU reference-check result;
the existing gem5 configs do not propagate simulated failure codes to the shell.
To run one case, use, for example, `run.sh pim 64`. To keep a second batch separate:

```bash
W1_RUN_TAG=w1_compare_repeat bash gem5/configs/scratch/progs/gpt3/w1_compare/run.sh all all
python3 gem5/configs/scratch/progs/gpt3/w1_compare/report.py --tag w1_compare_repeat
```

The CPU checks sampled outputs after timing. GEMV uses a wide reference; GEMM
uses the installed ZVL256B kernel's FP16 accumulation in 128-element K blocks,
with FP32 bias/output addition. Error against FP64 is also printed. This matches
`SHGEMM_Q=128` and `FP16_NARROW` in the installed OpenBLAS source; another kernel
build may require a different reference. The PIM
program checks the expected completion count and executes `m5_fail` if it differs.
PIMony performs no numerical arithmetic, so PIM outputs are not a numerical
validation; this is a timing experiment under the existing simulator assumptions.

To inspect the PIM binary after building, the following should show one LOAD
segment and no unused `Wq`, `Wk`, `Wv`, `Wo`, or `W2` symbols:

```bash
riscv64-unknown-elf-readelf -l gem5/configs/scratch/progs/gpt3/w1_compare/w1_pim_1
riscv64-unknown-elf-nm -S gem5/configs/scratch/progs/gpt3/w1_compare/w1_pim_1
```

## Interpretation

The original one-vector CPU run fails its numerical check, so the default
report rejects that batch. The valid 64-vector pair is recorded in RESULTS.md.
The checked one-vector alternative uses a local ordered-reduction GEMV kernel.
To reproduce that separate batch with a fresh output tag:

```bash
docker exec -w /home/pimony/gem5/configs/scratch/progs/gpt3/w1_compare gem5-container bash diagnostics/build_ordered_cpu.sh all
W1_FAST_INIT=1 bash gem5/configs/scratch/progs/gpt3/w1_compare/build_pim.sh
W1_FAST_INIT=1 W1_ORDERED_CPU=1 W1_RUN_TAG=w1_compare_ordered_repeat bash gem5/configs/scratch/progs/gpt3/w1_compare/run.sh all all
python3 gem5/configs/scratch/progs/gpt3/w1_compare/report.py --tag w1_compare_ordered_repeat
```

This batch uses Atomic initialization followed by O3 measurement. Its CPU GEMV
kernel differs from the original baseline, so its speedup must be identified
separately. The build helper regenerates the local kernel source and objects;
those generated files and the executables are excluded from version control.

The report divides CPU elapsed cycles by PIM elapsed cycles at each vector count.
A ratio above one means the PIM offload is faster. Comparing the ratios at 1 and
64 vectors tests whether increased CPU reuse changes the benefit of the current
GEMV offload. Two points cannot establish an exact crossover, and a W1 result
does not establish full-layer prefill speedup. Keep the earlier CPU-only sweep
as supporting characterization; do not directly combine its whole-layer timings
with this isolated projection benchmark.

Optional environment overrides: `W1_CPU_CC`, `W1_BLAS_ROOT`, `W1_PIM_CC` for
builds; `W1_CONTAINER`, `W1_CONTAINER_GEM5`, `W1_RUN_TAG` for runs.
