# Guarded cache-maintenance performance comparison

This bare-metal gem5 experiment compares three sequences over 1, 16 and 64
contiguous 64-byte cache lines on O3CPU and MinorCPU:

| Mode | Sequence |
|---|---|
| 0 | One `pim.fence.cl` per line |
| 1 | One `cbo.clean` followed by `fence iorw,iorw` per line |
| 2 | All `cbo.clean` operations followed by one `fence iorw,iorw` |

**Every measurement includes the same release-AMO completion guard:**
`amoswap.d.rl zero,zero,(scratch)`, using a warmed, separate cache line.
These are simulated costs of the complete guarded kernels, including loops
and measurement overhead. No empty-kernel cost is subtracted.

## Final measurements

Collected on 8 November 2026 at simulator commit
`7fa2ee9dc7c250c7f2e7ec6a802a25fccedb97fe` (`pim-isa`).
The 18 individual measurements and validation fields are preserved unchanged
in [validated/measurements.tsv](validated/measurements.tsv).

| CPU | Lines | Custom cycles | Standard per-line cycles | Standard batched cycles | Per-line vs custom | Batched vs custom |
|---|---:|---:|---:|---:|---:|---:|
| O3CPU | 1 | 90 | 96 | 96 | +6.67% | +6.67% |
| O3CPU | 16 | 1049 | 1100 | 239 | +4.86% | -77.22% |
| O3CPU | 64 | 4122 | 4316 | 546 | +4.71% | -86.75% |
| MinorCPU | 1 | 74 | 74 | 74 | +0.00% | +0.00% |
| MinorCPU | 16 | 500 | 961 | 500 | +92.20% | +0.00% |
| MinorCPU | 64 | 1940 | 3793 | 1940 | +95.52% | +0.00% |

Percent difference is `100 * (standard - custom) / custom`; negative means
fewer cycles for the standard sequence. `pim.fence.cl` is competitive with
standard per-line synchronization. Standard batching substantially reduces
simulated cost on O3CPU and ties custom cleaning on MinorCPU in this kernel.

## Machine and build

Runs use the isolated commit's unmodified `configs/pimony/fs_bert.py`, with
the synthetic ELF passed explicitly as the workload. BERT/GPT-3 workloads do
not execute; the configuration's BERT startup message is only a label.
The current working repository's modified configurations are never used.

| Setting | Value |
|---|---|
| CPU | `RiscvO3CPU` or `RiscvMinorCPU`; pipeline parameters from the pinned configuration/defaults |
| Clock | Requested 3 GHz; actual period 333 ticks at 1 THz tick frequency |
| Cache line | 64 bytes |
| L1I / L1D | Each 16 KiB, 2-way; tag/data/response latencies each 2 cycles |
| L1 MSHRs / targets | I: 4 / 20; D: 16 / 20 |
| Unified L2 | 2 MiB, 8-way; tag/data/response latencies each 20 cycles; 20 MSHRs, 12 targets |
| Prefetching | Stride prefetchers on L1D and L2, enabled in every mode |
| Memory | Timing mode; 2 GiB at `0x80000000`; existing PIMony controller and model configuration |
| Execution | M-mode bare metal, no checkpoint; one synthetic kernel per simulation |

Full O3/Minor configuration snapshots, memory/model JSON, compiler versions,
ELF/binary/library hashes, container image ID and build provenance are in
[validated/](validated/README.md). gem5 is version 25.1.0.1, rebuilt from the
clean isolated checkout. Host compiler: GCC 14.2.0; GNU objdump
2.45.50.20251209. All modes use:

```text
-O3 -std=gnu11 -march=rv64gc_zicbom -mabi=lp64d -mcmodel=medany
-ffreestanding -fno-builtin -fno-pie -no-pie -nostdlib -nostartfiles
-static -Wl,--no-relax -Wl,-T,<benchmark>/link.ld -DMODE=<0|1|2> -DNLINES=<1|16|64>
```

## Measurement and validation

Before reset, the benchmark dirties one 8-byte word in each operand line at
`0x81000000`, initializes scratch at `0x82000000`, then executes a setup FENCE
and consumed operand/scratch reads. Both loops disable GCC unrolling. The
measured interval is:

```text
m5_reset_stats -> selected maintenance sequence -> release-AMO guard -> m5_dump_reset_stats
```

Cycles are the first stats block's `simTicks / 333`; CPU `numCycles` agrees in
all 18 cases. The second block is only the exit interval. All final runs exited
normally with status zero and passed checks for identical machine settings,
expected instruction counts, N initially dirty lines, N unique `WriteClean`
creations, memory acceptance, N clean responses before the guard, and guard
completion before dump. Each configuration reproduced exactly once; these
are deterministic reproductions, not hardware confidence intervals.

Encodings with address in `a0`: custom clean `0x0005100b`, standard clean
`0x0015200f`, full FENCE `0x0ff0000f`. GNU objdump recognizes `cbo.clean` and
prints the custom word as `.insn`; gem5 Exec verifies `pim_fence_cl`. O3 Exec
records fetch timestamps even when emitted at commit, so instruction counting
uses reset/dump marker order; packet/PseudoInst times supply timing.

Both cleans use the CBMOp store path with `CLEAN|DST_POC`. Custom clean also
carries read/write barrier flags. Dirty lines emit `WriteClean`; clean
completion is to the modeled coherence point, not proof of physical DRAM-array
retirement. Backend exit summaries are not used for ROI timing.

## Why batching differs between CPUs

O3 handles a separate FENCE non-speculatively at the ROB head and waits for
older stores. Custom clean keeps its memory dependencies until the cache
response. In the 64-line traces, custom and standard per-line modes reach
only one outstanding L1 clean, while batching reaches sixteen. One final
barrier allows overlapping cache operations and amortizes synchronization;
this explains the large O3 batching benefit and the smaller extra cost of
standard per-line FENCE instructions.

Minor uses a store buffer and separate barrier requests. Custom and batched
modes both reach two outstanding L1 cleans, versus one for standard per-line
mode. Their guarded costs therefore tie here; custom clean does not serialize
every subsequent clean in these traces. The common release AMO waits for the
store buffer to drain and closes the measured interval for all three modes.
Its own cost and pipeline interaction remain included in every result.

## Reproduction

Use the existing clean isolated repository at `pim-isa-worktree/`, with its
rebuilt `gem5/build/RISCV/gem5.opt` and matching PIMony shared library. The
runner rejects a different commit, tracked source edits or a changed
`fs_bert.py`. It does not switch or modify the working repository's branch.
To use another prepared checkout, set `PIM_ISA_WORKTREE` to its repository root
(the directory containing `gem5/`). The commit and submodule revisions needed
for a fresh build are recorded in `validated/build_provenance.json`; the
successful gem5 build command was `scons build/RISCV/gem5.opt -j4`.

Dependencies: host RISC-V GCC/objdump and Python 3, Docker access, and the
installed `gem5_env:latest` image. The recorded image ID is in provenance.
From the working repository's `gem5/` directory:

```bash
bash tests/test-progs/fence_compare/run.sh
```

Defaults run both CPUs, all three sizes and all modes. Optional environment
variables: `RISCV_CC`, `RISCV_OBJDUMP`, `CPU_MODELS`, `LINE_COUNTS`,
`PIM_ISA_WORKTREE`, and `RESULTS_DIR` (beneath this benchmark). Each invocation
creates a new result directory and refuses to overwrite an existing one. It
saves source snapshots, compile commands, ELFs/disassembly, provenance,
configuration, raw traces, stats and analysis. `run_config.py` redirects PIMony
logs and caps simulation at 100 million ticks. Docker networking is disabled.
The bundled startup/linker files match the measured originals; packaging only
changes their paths and adds provenance checks. No new measurements were made
during integration.

## Scope and preserved diagnostics

The guard supplies a common completed ROI; it does not establish universal
architectural equivalence of the unguarded sequences or real hardware speed.
Earlier Minor cacheable/MMIO probes exposed incomplete ROIs and a real generic
memory-ordering issue involving barrier tracking. The guard does not repair
that implementation. The separate real-GEMV investigation checked launch,
commands, tokens and input visibility, but its numerical assertions **failed**:
output remained the sentinel. Those failures are not passing regression tests
and are excluded from this performance comparison.

Existing artifacts remain locally at:

- `results/run_20261108_guarded_final/`: original final logs, source snapshots and measurements.
- `results/pilot_20261108_guard/` and `results/run_20261108_guarded_repeat1/`: exact guarded reproductions.
- `results/run_20261108_primary/`, `results/run_20261108_repeat64/`, `results/pilot_20261108_mmio/`: earlier methodology diagnostics.
- `results/REPORT_FOR_REVIEW.md`: original detailed performance report.
- `e2e/results/REPORT.md` and `e2e/results/repro_20261108/INVESTIGATION.md`: separate GEMV failures and Minor ordering investigation.
- `results/integration_*/`: pre-integration files, Git state and preservation checks.

`results/`, `e2e/`, the isolated worktree and generated caches are ignored by
Git. Only benchmark sources and compact validated artifacts are intended for
version control; no simulator, existing workload or thesis evaluation result
is changed.
