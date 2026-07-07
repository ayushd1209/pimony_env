# PIM baremetal — performance baseline

Baseline for the host-microarchitecture comparison (async PIM). Same binary
(`pim_baremetal`) is run across CPU models; only the CPU model in
`configs/pimony/fs_pimony_baremetal.py` changes.

## Workload
- Test: 4.3c isolation test (`main.c`, `NPROC=2`) — 2 processes (ASID 1, 2),
  each dispatches 1 MAC token; foreign `pim.wait` must return -1, owner 0; `m5_exit` on pass.
- Config: no caches, DRAM 512MB @ 0x80000000, PIMony, PIM reg window 0x100000000,
  Sv39 paging, S-mode, interrupt-driven completion. Clock = 1 GHz (1000 ticks/cycle).

## Baseline: RiscvTimingSimpleCPU  (in-order, non-pipelined)  — 2026-07-05
| metric | value |
|---|---|
| simTicks (finalTick) | **140,451,000** |
| simulated time | 140.45 µs |
| numCycles | 140,451 |
| simInsts / simOps | 2,374 |
| CPI | 59.16 |
| IPC | 0.0169 |
| hostSeconds | 0.92 |

Note: CPI ~59 / IPC ~0.017 is expected and telling — the host spends almost all
cycles **quiesced in `pim.wait`** waiting for async PIM completion, retiring very
few instructions. i.e. with blocking `pim.wait`, the host does ~no useful work
during PIM latency. This is the overlap/utilization angle for "best host."

## RiscvMinorCPU  (detailed in-order pipeline, **WITH L1 caches**)  — 2026-07-06
Config: fs_pimony_baremetal_cached.py (16KiB L1 I+D, PIM window uncacheable via PMAChecker).
| metric | value |
|---|---|
| simTicks | 4,544,000 |
| numCycles | 4,544 |
| simInsts / simOps | 2,374 (identical to baseline ✓ — no speculative re-exec) |
| CPI | 1.91 |
| IPC | 0.52 |

**CAVEAT — not apples-to-apples yet.** Two variables changed vs the baseline:
TimingSimple→Minor AND cacheless→cached. The ~31x speedup is dominated by the
CACHES (cacheless = every instr fetch is a slow DRAM read → CPI 59), not by the
pipeline. To isolate the microarchitecture effect, compare SAME cache config
across CPUs (e.g. TimingSimple-cached vs Minor-cached vs O3-cached).
MinorCPU on the cacheless config CRASHES (PIMony phantom-completion assert) — caches were required to bring it up.

## RiscvTimingSimpleCPU (cached)  — 2026-07-06  [apples-to-apples in-order baseline]
Config: fs_pimony_baremetal_cached.py `timing` (same L1 caches as the Minor run).
| metric | value |
|---|---|
| simTicks | 9,728,000 |
| numCycles | 9,728 |
| simInsts | 2,374 |
| CPI / IPC | 4.10 / 0.244 |

## RiscvO3CPU  (out-of-order, cached)  — 2026-07-06
Config: fs_pimony_baremetal_cached.py `o3`.
NOTE: gem5 default BaseO3CPU, no param overrides — 8-wide (fetch/decode/rename/
dispatch/issue/wb/commit=8), ROB=192, LQ/SQ=32, ~256 phys regs, default branch
predictor. So this is a generic AGGRESSIVE OoO, NOT tuned to any specific target
chip. For this wait-dominated test the width barely matters; it will matter in the
overlap experiment. To model a specific RISC-V core, override these params.
| metric | value |
|---|---|
| simTicks | 2,891,000 |
| numCycles | 2,892 |
| simInsts | 2,373 (benign ±1 vs 2374 — quiesce/squash counting) |
| CPI / IPC | 1.22 / 0.82 |
Speculation check: dispatched tokens are 0,1 (NOT skipped) → no wrong-path token
over-allocation even on O3. `pim.dispatch` side-effect is safe in practice.

## SUMMARY (numCycles) — same L1 caches for the bottom three (fair µarch compare)
| config | numCycles | IPC | note |
|---|---|---|---|
| TimingSimple, cacheless | 140,451 | 0.017 | original baseline |
| TimingSimple, cached | 9,728 | 0.24 | **~14x = caches alone** |
| Minor, cached | 4,544 | 0.52 | ~2.1x over TS-cached = µarch |
| O3, cached | 2,892 | 0.82 | ~3.4x over TS-cached; ~1.6x over Minor |

Takeaway: caches dominate (~14x); µarch ladder adds ~3.4x on top. All still small
& WAIT-DOMINATED (2374 insts, hart blocks in pim.wait). Host µarch only speeds the
instruction overhead, not the fixed PIM latency → the real "best host" lever is
OVERLAP (host doing useful work during the PIM wait), not raw core speed.

**Interpretation:** caches dominate (~14x); pipeline µarch is only ~2x. This test
is WAIT-DOMINATED (2374 insts, hart quiesces in pim.wait for fixed PIM latency),
so host µarch only speeds the instruction overhead, not the PIM latency. For host
µarch to matter for async PIM, need an OVERLAP workload (host doing useful work
during the PIM wait) — the missing experiment for a real "best host" claim.

## What to compare
- **simInsts should be ~identical** across cores (same program). If it differs on
  Minor/O3, suspect speculative re-execution (e.g. `pim.dispatch` token side-effect).
- **simTicks / numCycles** differ = the microarchitecture's effect on the same work.
- **CPI/IPC** here mostly reflect wait-time, not core throughput — for a real
  throughput comparison, add a host-side workload to run *during* PIM latency (overlap).
