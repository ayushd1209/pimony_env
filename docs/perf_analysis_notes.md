# Performance analysis notes — PIM instruction set on gem5

Working notes: how to read the output, what has been measured, and the traps found
along the way. Raw material for the report. Grows as we go.

Dataset: `gem5/m5out/fix_{timing,minor,o3}/` — matched runs of
`configs/pimony/fs_fence_e2e.py`, one per CPU model, each with `run.log` + `stats.txt`.

---

## 1. The two output files are different in kind

| file | what it is |
|---|---|
| `run.log` | a **diary** — timestamped events in the order they happened |
| `stats.txt` | a **scoreboard** — totals at the end of the run, no time information |

The diary tells you the story; the scoreboard tells you the totals. You need both.
`stats.txt` is overwritten every run, so always use `-d <dir>` to give each run its
own output directory.

## 2. Reading a diary line

```
 865000: system.mem_ctrl: PIM dispatch addr=2164260864 num_macs=64 token=0 asid=1
 └──┬──┘  └──────┬─────┘  └────────────────────┬──────────────────────────────┘
   when        who                            what
```

`who` is the SimObject name from the config script — every line is attributable to a
box in the design.

## 3. Reading a scoreboard line

Every gem5 stat name has the same four-part shape:

```
system.cpu.dcache . InvalidateReq . mshrMissLatency :: total     52000
└──────┬─────────┘   └─────┬────┘   └──────┬──────┘    └─┬─┘
   component         request kind      measurement    requestor
```

1400 lines becomes a grid you navigate, not a list you read.

## 4. Three clocks in one file — the #1 source of wrong numbers

| unit | size | appears as |
|---|---|---|
| tick | 1 ps | `865000:` at line start |
| CPU cycle | 1000 ticks (1 GHz) | never printed — divide |
| DRAM cycle | 0.937647 ns | `clk: 854` in DRAMsim3 lines |

Cross-check: `854 × 0.937647 ns = 800.7 ns = 800,700 ticks` ≈ the gem5-side write at
`800000`. Do this conversion once in the report — it validates the coupling and shows
the two time bases are understood.

## 5. Validity gates — run these before quoting any performance number

1. **Exit reason** — `grep "Exiting @"` must say `m5_exit instruction encountered`.
2. **`simInsts` identical across CPU models** — same deterministic program ⇒ same
   dynamic instruction count. A difference means a bug, not a µarch effect.
3. **Exactly one dispatch, one completion** — and check the *token value* too; a count
   of 1 does not catch a replayed allocation.
4. **Ordering invariant** — `WRITE 0x81000000` before `PIM dispatch`; final `READ`
   after `PIM token N complete`.
5. **Fence packets escaped L1** — `l2bus.transDist::{CleanSharedReq,InvalidateReq,WriteClean}`
   each = 1. Distinguishes "the instruction retired" from "the instruction did something".

## 6. Mapping source lines to log signatures

`tests/test-progs/fence_e2e/main.c` is six steps. Lining them up against the log is
the whole skill.

| source step | log signature |
|---|---|
| 1. `*op = 0x1234` | DRAM **read** of `0x81000000` (the line fetch — a write needs the line first) |
| 2. `pim_fence_cl` | DRAM **WRITE** of `0x81000000` |
| 3. `pim_dispatch` | `PIM dispatch … token=N`, then a `MAC` command |
| 4. `pim_wait` | **nothing** — see gotcha 8.1 |
| 5. `pim_fence_inv` | nothing in DRAM (caches only) |
| 6. `r = *op` | second DRAM **read** of `0x81000000` |

Direction lives in the `complete` line (`Read to…` / `Write to…`) and in DRAMsim3's
`(RD)`/`(WR)`/`(PIM)` tag. `Enqueueing` and `Access for` never state direction.

---

## 7. Verified measurements

All three models, matched runs, post token-fix.

| | timing | minor | o3 |
|---|---|---|---|
| total (`simTicks`) | 2,563,000 | 2,768,000 | 2,110,000 |
| instructions committed | 470 | 470 | 470 |
| DRAM reads / writes | 9 / 1 | 10 / 1 | 9 / 1 |
| instruction lines fetched | 5 | 6 | 5 |
| dispatch token | 0 | 0 | 0 |

### PIM engine latency — device property, not core artifact

| model | dispatch → completion | cycles |
|---|---|---|
| timing | 865,000 → 1,155,321 | 290.3 |
| minor | 896,000 → 1,186,242 | 290.2 |
| o3 | 811,000 → 1,100,975 | 290.0 |

Spread 0.12% across three very different pipelines. Defensible as a hardware property.

### pim.fence cost

True cost requires `mshrMisses = 1` (see gotcha 8.2):

- `pim.fence.cl` = **57 cycles** (L1 issue → PoC)
- `pim.fence.inv` = **52 cycles** — confirmed independently by timing *and* minor,
  both exactly 52,000 ticks
- L2 → memory component: **34 / 29 cycles**, bit-identical on all three models ⇒
  the fence's memory-side cost is a property of the hierarchy, not of the core

### Phase decomposition (timing, total 2563 cycles)

| phase | window (ticks) | cycles |
|---|---|---|
| boot, paging, icache cold | 0 → 679,000 | 679 |
| operand store (line fetch) | → 739,293 | 60 |
| `pim.fence.cl` → DRAM | → 801,135 | 174 gross / 57 true |
| dispatch accepted | 865,000 | — |
| **PIM MAC execution** | → 1,155,321 | **290** |
| **completion trap handler** | → ~2,516,000 | **~1,361** |
| `fence.inv` + re-read + exit | → 2,563,000 | 47 |

### Headline finding — completion cost dominates

412 of 470 instructions (**87.7%**) are inside `trap_handler`, proved by instruction
count from an `Exec` trace, not inferred. Cost of the completion path vs the PIM
compute it reports:

| model | handler | MAC | ratio |
|---|---|---|---|
| timing | 1361 | 290 | 4.7× |
| minor | 1542 | 290 | 5.3× |
| o3 | 938 | 290 | 3.2× |

44–56% of total runtime. Motivates a hardware "next completed token" register in place
of the 64-bit-mask software scan in `main.c`.

### Asynchrony — capability proven, benefit not yet measured

- dispatch response returned at 873,000, **8 cycles** after issue at 865,000, while the
  MAC ran until 1,155,321 ⇒ genuinely non-blocking
- the CPU issued its own instruction fetch (`0x80000100`) at 923,000, *inside* the MAC
  window ⇒ real overlap, two agents in the memory system at once
- but `quiesceCycles` = 250 (minor) / 247 (o3) of the 290-cycle window ⇒ the core is
  **asleep** for most of it

The source is `dispatch; wait;` with nothing between, so there is no independent host
work to overlap **by construction**. State this explicitly; the experiment shows the
capability, not its value.

---

## 8. Gotchas found (each one would have produced a wrong number)

**8.1 Absence in a log is not absence in the machine.** `--debug-flags=DRAMsim3` means
only the memory controller speaks. `pim_wait` never contacts it, so it appears nowhere.
Measure invisible instructions by bracketing them between events you *can* see.

**8.2 CMO latency stats inflate ~3× when `mshrHits = 1`.** That means the CMO merged
into an MSHR already outstanding for the same line, so the latency includes waiting on
the older transaction. Only `mshrMisses = 1` measures the operation itself. Also
`avgMissLatency` reads `inf` for CMOs (latency accumulates while `misses::total` stays
0) — never paste the `inf`.

**8.3 `numCycles` is not comparable across CPU models.** timing reports the whole
runtime (2563 = `simTicks`/1000); minor and o3 report *active* cycles only and track
sleep separately (2518 + 250 = 2768). Taken at face value, `numCycles` says minor is
faster than timing when it is 205 cycles slower. **Compare `simTicks`.**

**8.4 A "purely internal" change can move timing.** Fixing the token allocator changed
o3's token from 1 to 0 and the runtime from 2,129,000 to 2,110,000. Not a speedup: the
handler does `if (mask & (1ULL<<t))`, so the taken iteration moved from `t=1` to `t=0`,
costing 2 fewer branch mispredicts (24→22) and 15 fewer squashed instructions (148→133).
Same 470 instructions, same memory traffic. **Do not report as a performance gain.**

**8.5 Finding a cause is not finding the whole cause.** minor is 205 cycles slower than
timing; the extra instruction fetch explains only 43. Attributing the whole gap to the
first mechanism found would have been wrong. (Remainder: see open questions.)

**8.6 `pim.dispatch` silently cleans its own cache line — a fence A/B with a single-line
operand is invalid.** The dispatch is `UNCACHEABLE`, and its address *is* the operand
base (`EA = rvZext(Rs1)`; `pimony.cc` uses `pkt->getAddr()` as the MAC's DRAM address).
gem5's classic cache evicts any resident block at an uncacheable access's address —
`Cache::access`, `src/mem/cache/cache.cc:166-185`, `evictBlock()` → `WritebackDirty` if
dirty, `CleanEvict` if not. Confirmed in `m5out/freebie.log` (`--debug-flags=Cache`):

```
786000: system.cpu.dcache: access for ReadReq [81000000:81000007] UC
786000: system.cpu.dcache: Create CleanEvict CleanEvict [81000000:8100003f]
789000: system.l2cache:    Create CleanEvict CleanEvict [81000000:8100003f]
```

Eviction at **both** cache levels, caused by nothing but the dispatch passing through.
`CleanEvict` rather than `WritebackDirty` only because `pim.fence.cl` had already cleaned
the line at 726000 (`WriteClean … dirty: 1` — evidence the fence does real work).
The freebie covers **line 0 only**; lines 1..N are untouched.

Consequence for the evaluation: with a 64-byte operand the dispatch cleans all of it and
"fence vs no fence" measures nothing. With 32 lines the fence does 31 lines of essential
work. **Same instruction, opposite conclusion, decided purely by operand size — always
use a multi-line operand and state its size next to any fence number.**

Classification: this is not architectural. Mixing cacheable and uncacheable accesses to
one physical address is CONSTRAINED UNPREDICTABLE in ARM, discouraged in x86, unpromised
by RISC-V PMA. gem5 picked "flush the line"; other implementations need not. **Design as
if it does not happen** — `pim.fence` owns all cache management. Do not patch `cache.cc`:
that path serves every uncacheable access in the simulator. The real fix is the address
split (see open question 5).

---

## 9. What is not claimable from this dataset

- **Cross-model IPC / CPI comparison.** 470 instructions, ~2000 cycles, no steady
  state. Differences are cold-cache and interrupt-timing noise. If asked why no
  performance comparison across cores: this is the answer, and it is a good one.
- **PIM throughput.** `num_macs = 64` counts 256-bit column steps and the engine does
  no arithmetic — it is a timing model. Say *latency*, never FLOPS.
- **DRAMsim3 `AVG BW Util 0.00%`.** A rounding artifact of a 2736-DRAM-cycle run.
- **`comp_energy`** appears on channel 0 only, correctly localising the MAC — usable,
  but flag it as a model estimate from the config's pJ coefficients.

## 10. Open questions

1. **162 of the 205-cycle timing↔minor gap is unattributed.** 43 cycles come from
   minor's extra instruction fetch (run-ahead fetch pulls a line nobody uses).
2. **Overlap has never been measured**, only shown to be possible. Needs independent
   host work between `dispatch` and `wait`. This is the thesis critical path.
3. **The MAC phase is a single data point** (290 cycles at `num_macs = 64`). A sweep
   would make it a curve.
4. **Replay path is untested on timing and minor** — neither replays in this workload,
   so the token fix is inert there. Needs a workload where the dispatch bounces.
5. **Doorbell address vs operand address — deferred spec decision** (cause of 8.6). The
   dispatch's address field does two jobs: bus routing *and* operand pointer. Splitting
   them removes the hidden eviction with no gem5 edit — route on `PIM_REG_BASE`
   (`0x100000000`, no program data ever cached there) and carry the operand pointer in
   the payload. Blocked on payload width: `[63:48] token | [47:32] asid | [31:0]
   num_macs` is full, so it needs a wider descriptor or the real-hardware two-step
   (write address reg → write config reg → ring doorbell). Touches `decoder.isa`,
   `pimony.cc`, and every test binary — decide when writing the instruction spec, not
   before. Alternative if kept as-is: state in the spec that `pim.dispatch` cleans the
   line at its operand address, and implement that deliberately in RTL.

---

## Tooling

`gem5/pim_report.sh table <m5out dirs>` — side-by-side stats table
`gem5/pim_report.sh phases <run.log>` — phase timeline with deltas
