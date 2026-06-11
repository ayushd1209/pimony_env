# PIMony Memory-Side Reference (for a CPU person)

> **Purpose:** everything below the CPU that you need to know for the thesis
> (`pim.dispatch` / `pim.wait` custom RISC-V instructions). Verified against
> the code on 2026-06-11. File:line references are clickable in the IDE.

---

## 1. The one-picture summary

```
 ┌─────────────────────────────────────────────────────────────────────────┐
 │  YOUR WORLD (CPU)                                                       │
 │  pim.dispatch / pim.wait in src/arch/riscv/isa/decoder.isa (~line 2191) │
 │  → builds a memory Request with flag PIM_DISPATCH (src/mem/request.hh:190)│
 └────────────────────────────┬────────────────────────────────────────────┘
                              │ gem5 Packet (timing port protocol)
                              ▼
 ┌─────────────────────────────────────────────────────────────────────────┐
 │  GEM5 GLUE  —  src/mem/   (simulator plumbing, no hardware equivalent   │
 │               except the controller FRONT-END behavior)                 │
 │                                                                         │
 │  PIMony.py          SimObject declaration. Python type is literally     │
 │                     `DRAMsim3` (half-renamed fork!). Params:            │
 │                     mem_config, model_config, log_dir, log_level        │
 │                                                                         │
 │  pimony.hh/.cc      class gem5::memory::DRAMsim3 : AbstractMemory       │
 │                     = controller front-end: port, queues, retries,      │
 │                       clock tick, completion callbacks                  │
 │                                                                         │
 │  pimony_wrapper.hh/.cc  class DRAMsim3Wrapper — namespace shim that     │
 │                     holds a pimony::MemorySystem* and forwards calls    │
 └────────────────────────────┬────────────────────────────────────────────┘
                              │ C++ calls into libdramsim3.so
                              ▼
 ┌─────────────────────────────────────────────────────────────────────────┐
 │  THE ACTUAL MODEL OF HARDWARE — ext/dramsim3/PIMony/                    │
 │                                                                         │
 │  src/  (top layer, namespace pimony)                                    │
 │    MemorySystem      public API gem5 calls (memory_system.h)            │
 │    Request handler   queues normal vs PIM traffic, tracks completion    │
 │    LLMGenerator      built-in GEMV/GPT trace generator (model_config)   │
 │    Address           addr → channel/rank/bankgroup/bank/row/col         │
 │    PIM (Dram.h/cc)   wraps the low-level simulator                      │
 │                                                                         │
 │  PIMSim/  (low layer, namespace dramsim3 — cycle-accurate DRAM+PIM)     │
 │    JedecDRAMSystem → per-channel PIMController                          │
 │    PIM command queue, MAC units, JEDEC LPDDR5/5X timing                 │
 │                                                                         │
 │  Both compile together into ONE library: libdramsim3.so                 │
 └─────────────────────────────────────────────────────────────────────────┘
```

**Restaurant version:** CPU = customer, `src/mem/` = waiter,
`ext/dramsim3/PIMony/` = kitchen. One PIM, one messenger to reach it.

---

## 2. Naming gotcha (read this before grepping)

The `src/mem/pimony*` files are gem5's stock **DRAMsim3 integration, forked
and only the *filenames* renamed**. Inside:

| What you'd expect | What it's actually called | Where |
|---|---|---|
| `class PIMony` | `class DRAMsim3` | [pimony.hh:61](src/mem/pimony.hh#L61) |
| Python `PIMony(...)` | Python `DRAMsim3(...)` | [PIMony.py:40-43](src/mem/PIMony.py#L40-L43) |
| `PIMonyWrapper` | `DRAMsim3Wrapper` | [pimony_wrapper.hh:72](src/mem/pimony_wrapper.hh#L72) |
| debug flag `PIMony` | debug flag `DRAMsim3` | `--debug-flags=DRAMsim3` |

The engine itself lives in C++ `namespace pimony` (and its inner DRAM core in
`namespace dramsim3`). In config scripts you instantiate `DRAMsim3(...)`.

---

## 3. The interface you care about (CPU ↔ memory)

### 3a. Downward path: how your instruction reaches the PIM

1. **`pim.dispatch rd, rs1, rs2`** ([decoder.isa:~2191](src/arch/riscv/isa/decoder.isa#L2191))
   issues a memory access with `mem_flags=[PIM_DISPATCH, UNCACHEABLE]`.
   rs1 = target address, rs2 = size in bytes (carried as the 8-byte store
   payload), rd = token.
2. The flag is `Request::PIM_DISPATCH = 0x08000000`
   ([request.hh:190](src/mem/request.hh#L190)).
3. The packet arrives at `DRAMsim3::recvTimingReq()`
   ([pimony.cc:195](src/mem/pimony.cc#L195)). The PIM intercept is at
   [pimony.cc:215-234](src/mem/pimony.cc#L215-L234):
   - reads `size_bytes = pkt->getLE<uint64_t>()` from the payload,
   - calls `wrapper.enqueuePIM(addr, size_bytes)`,
   - **immediately acks the CPU** (response queued same tick, no backing-store
     write) — this is what makes the dispatch *asynchronous*.
4. `DRAMsim3Wrapper::enqueuePIM()` ([pimony_wrapper.cc:129](src/mem/pimony_wrapper.cc#L129))
   → `dramsim->AddMACTransaction(addr, (uint32_t)size_bytes)`.
5. Inside the engine that becomes a `TransactionType::MAC` →
   `CommandType::MAC`, queued in the per-channel **PIM command queue** and
   executed by MAC units (see §7).

> ⚠️ Note the narrowing in step 4: size is cast `uint64_t → uint32_t`.
> The `size→num_macs` conversion (size·8/elem_bits) happens inside PIMony.

### 3b. Upward path: how completion comes back

The engine signals up through **three callbacks** registered at construction
([pimony.cc:53-60](src/mem/pimony.cc#L53-L60), threaded through the wrapper
into `pimony::GetMemorySystem(...)`):

| Callback | Signature | gem5 handler | What it does now |
|---|---|---|---|
| `read_cb` | `void(uint64_t addr)` | `readComplete()` [pimony.cc:345](src/mem/pimony.cc#L345) | pops `outstandingReads[addr]`, does the real data access, sends response packet to CPU |
| `write_cb` | `void(uint64_t addr)` | `writeComplete()` [pimony.cc:371](src/mem/pimony.cc#L371) | bookkeeping only (writes were acked early) |
| `pim_cb` | `void()` | `pimComplete()` [pimony.cc:339](src/mem/pimony.cc#L339) | **`exitSimLoop("PIM_DONE", 0)` — ends the simulation!** |

> 🔴 **Critical for `pim.wait`:** the current `pimComplete()` is a placeholder
> that kills the sim loop. Your interrupt-driven completion (per the pim.wait
> design) must replace this with: post an interrupt / wake the hardware
> thread / mark the token done in the scoreboard. This function is *the* seam
> where memory-side completion meets the CPU side.
>
> Also note `pim_cb` carries **no token/address** — it fires once when *all*
> PIM ops are done (engine-internal `is_pim_operation_done()`). For a
> multi-token scoreboard you'll need to widen this signature (e.g.
> `void(uint64_t token)`), through all three layers: engine → wrapper → gem5.

### 3c. Normal reads/writes (for completeness)

- **Read:** queued in `outstandingReads`, `wrapper.enqueue(addr, false)`;
  data access + response happen only at `readComplete` (timing-accurate).
- **Write:** acked to CPU immediately (`accessAndRespond` at enqueue time),
  engine models drain timing in the background.
- **Backpressure:** controller accepts at most `wrapper.queueSize()`
  outstanding transactions ([pimony.cc:212](src/mem/pimony.cc#L212)); above
  that it nacks and sends `retryReq` later. **PIM dispatches bypass this
  check** (the intercept comes before the `can_accept` branch) — be aware if
  you ever flood dispatches.
- **FIFO assumption:** completions are matched to packets per-address FIFO
  ([pimony.cc:354](src/mem/pimony.cc#L354)) — fine in practice, noted as
  approximate in the comments.

### 3d. Clocking — how two simulators stay in sync

gem5 calls `wrapper.tick()` → `MemorySystem::ClockTick()` once per DRAM clock,
self-rescheduled at `curTick() + clockPeriod_ns` ([pimony.cc:151-169](src/mem/pimony.cc#L151-L169)).
The DRAM clock period is *pulled from the engine* (`GetTCK()`), not set in
gem5. Constraint enforced at init: **engine burst size must equal the system
cache-line size** ([pimony.cc:91-93](src/mem/pimony.cc#L91-L93)) — burst =
busBits × BL / 8 (e.g. 32 bit × 16 / 8 = 64 B).

### 3e. The complete wrapper API surface

Everything gem5 can ask of the engine ([pimony_wrapper.cc](src/mem/pimony_wrapper.cc),
engine side `ext/dramsim3/PIMony/src/memory_system.h`):

```cpp
// construction (registers the 3 callbacks)
GetMemorySystem(mem_config, model_config, log_dir, log_level,
                pim_cb, read_cb, write_cb);
bool  WillAcceptTransaction(uint64_t addr, bool is_write);  // canAccept()
bool  AddTransaction(uint64_t addr, bool is_write);         // enqueue()
bool  AddMACTransaction(uint64_t addr, uint32_t num_macs);  // enqueuePIM()
void  ClockTick();                                          // tick()
double GetTCK();  int GetQueueSize();  int GetBusBits();  int GetBurstLength();
void  PrintStats();  void ResetStats();
```

If you add a new capability (e.g. token-tagged dispatch, status query for
`pim.wait` polling), it must be added **in all three places**: the engine's
`MemorySystem`, the wrapper, and `pimony.cc`.

---

## 4. "Memory controller" — where exactly is it? (hardware ↔ file mapping)

A real LPDDR memory controller in an SoC sits between the interconnect and
the DRAM PHY, and is conventionally split in two:

```
CPU ──interconnect──▶ ┌────────── MEMORY CONTROLLER ──────────┐ ──PHY──▶ DRAM dies
                      │ FRONT-END            BACK-END         │          (+ PIM MACs
                      │  bus interface        command scheduler│           in the dies)
                      │  request queues       (FR-FCFS etc.)   │
                      │  flow control         bank-state track │
                      │  write buffer         timing enforce   │
                      │  response path        refresh engine   │
                      └───────────────────────────────────────┘
```

In this codebase that split lands as: **front-end behavior ≈ `pimony.cc`,
back-end + DRAM device ≈ the `ext/` engine.** `pimony.cc` is not *labeled* a
controller front-end anywhere — it's an `AbstractMemory` — but every behavior
it implements is exactly what a controller front-end does in silicon:

| Real hardware block | What it does in silicon | Where it's modeled |
|---|---|---|
| **Bus slave interface** (AXI/CHI port) | accepts request beats, handshakes valid/ready | `MemoryPort` + `recvTimingReq()` [pimony.cc:195](src/mem/pimony.cc#L195), [pimony.cc:414-451](src/mem/pimony.cc#L414-L451) |
| **Admission / flow control** | "queues full → stall the requester, signal retry" | `nbrOutstanding() < wrapper.queueSize()` [pimony.cc:212](src/mem/pimony.cc#L212); nack + `retryReq`, retry sent when space frees in `tick()` [pimony.cc:160-164](src/mem/pimony.cc#L160-L164) |
| **Transaction table** (in-flight request tracking, like MSHRs) | remembers which requests are outstanding so responses can be matched | `outstandingReads` / `outstandingWrites` maps [pimony.cc:241](src/mem/pimony.cc#L241), [pimony.cc:253](src/mem/pimony.cc#L253) |
| **Posted-write buffer** | acks a write to the bus before DRAM actually commits it | write path calls `accessAndRespond()` at enqueue time [pimony.cc:258](src/mem/pimony.cc#L258); engine drains it in background |
| **Response channel** (with backpressure) | buffers read data beats until the bus can take them | `responseQueue` + `sendResponse()`/`retryResp` [pimony.cc:112-142](src/mem/pimony.cc#L112-L142) |
| **Doorbell / command-register decode** | detects "this write is a device command, not a data write" | the `PIM_DISPATCH` flag check [pimony.cc:215](src/mem/pimony.cc#L215) — your dispatch is effectively a doorbell write |
| **Command scheduler** (FR-FCFS, ACT/RD/WR/PRE generation, bank-state machine, timing enforcement) | the back-end "brain" | **ext/**: `PIMController` + command queues, `PIMSim/src/pim_controller.cc` |
| **Refresh engine** | issues REFRESH per tREFI | **ext/**: PIMSim (`REFRESH`/`SREF_*` commands, `.ini` timing) |
| **PIM ↔ memory arbitration** | who gets the shared banks | **ext/**: `scheduling_policy` (PIM_FIRST/MEM_FIRST) in `pim_controller.cc` |
| **The DRAM dies themselves** (banks, rows, sense amps, per-bankgroup MAC units) | the memory + PIM silicon | **ext/**: PIMSim bank/timing model + `MacState` units |

So when you say "pimony.cc is sort of like a memory controller front-end" —
yes, precisely: it models the *bus-facing half* of the controller, while the
*DRAM-facing half* (scheduling, timing, refresh) and the DRAM device itself
live in `ext/`.

### What has NO hardware counterpart (pure simulator artifacts)

| Thing | Why it exists |
|---|---|
| `DRAMsim3Wrapper` (pimony_wrapper) | C++ namespace isolation between two codebases — in hardware the front-end and back-end are just wires apart |
| The three `std::function` callbacks | in silicon, completion is a response packet on a wire; callbacks are how two software simulators signal each other |
| `tick()` co-simulation loop | keeps gem5's event queue and the engine's cycle loop in lockstep — hardware has one clock tree, simulation has two schedulers |
| `PIMony.py` SimObject params | configuration plumbing |

### One subtlety: timing and data are SPLIT between the two sides

In real hardware, the data physically lives in the DRAM dies. In this
simulation it does not:

- **Data** lives in gem5's `AbstractMemory` backing store and is read/written
  by `access(pkt)` inside `accessAndRespond()` [pimony.cc:308](src/mem/pimony.cc#L308)
  — functionally correct, zero timing.
- **Timing** lives in the `ext/` engine, which only ever sees *(address,
  is_write)* or *(address, num_macs)* — **never the data bytes**.

Consequence that matters for your thesis: the PIM MAC units are
**timing-only**. A `pim.dispatch` makes the engine *charge cycles* for
`num_macs` multiply-accumulates, but no actual arithmetic happens on the data
at those addresses — gem5's backing store is untouched. If you ever need
functionally-correct PIM results (e.g. a benchmark that checks output values),
the computation has to be done on the gem5 side (e.g. in `pimony.cc` against
the backing store at dispatch or completion time), with the engine providing
only the latency.

---

## 5. Engine structure — top layer (`ext/dramsim3/PIMony/src/`)

| Class | File | Models / does |
|---|---|---|
| `MemorySystem` | `memory_system.h/cc` | public API; per-cycle loop: feed requests per channel → `dram->cycle()` → drain per-channel response queues → fire callbacks |
| `PIM` (extends `Dram`) | `Dram.h/cc` | owns the low-level `dramsim3::PIMSim`, per-channel push/top/pop, bandwidth stats |
| `Request::TraceRequestHandler` | `Request.h/cc` | queues normal vs PIM traffic, tracks latency/SLO, knows `is_pim_operation_done()` |
| `LLM::LLMGenerator` | `LLM.h/cc` | **built-in workload generator** — turns model_config (GPT-3/GEMV json) into H2GWRITE + COMP/MAC + READRES traces |
| `ADDRESS::Address` | `Address.h/cc` | physical addr ↔ (ch, rank, bg, bank, row, col) |
| `Common.h` | — | `MemoryAccessType` enum, `TraceEntry`, `MemoryAccess` structs |

Two operating styles coexist:
- **Standalone** (`build/pimony` executable): trace-driven, generates its own
  LLM workload — what the PIMony paper-style experiments use.
- **gem5-linked** (`libdramsim3.so`): gem5 injects transactions via
  `AddTransaction`/`AddMACTransaction`. *The LLM generator still exists and is
  configured by `model_config`* — relevant if PIM background traffic appears
  that you didn't dispatch.

Quirk worth knowing: `AddTransaction` (writes) **splits each request across
two partner channels** (even/odd pairing) and coalesces both completions
before the callback fires (memory_system.cc `AddTransaction`, ~lines 223-261).

---

## 6. Engine structure — low layer (`ext/dramsim3/PIMony/PIMSim/`)

A DRAMsim3 derivative (`namespace dramsim3`) doing cycle-accurate JEDEC
timing. Entry: `PIMSim/include/pimsim/PIMSim.h`.

```
PIMSim
 └─ JedecDRAMSystem            (dram_system.h)
     └─ PIMController × N channels   (pim_controller.h/cc)
         ├─ normal command queue  (ACT/RD/WR/PRE, refresh)
         ├─ PIM command queue     (pim_command_queue.h/cc)
         └─ MacState units        (the modeled PIM hardware, §6)
```

DRAM command set extended with PIM commands (`PIMSim/src/common.h:64-82`):

```
normal:  READ, WRITE, ACTIVATE, PRECHARGE, REFRESH, SREF_*
PIM:     D2GWRITE   DRAM → global buffer write
         H2GWRITE   host → global buffer write (carries num_macs)
         COMP       compute, all-bank synchronous
         MAC        multiply-accumulate, async per-bankgroup  ← your dispatch
         MACINTR    MAC interrupt / bankgroup coordination
         READRES    read result from global accumulator
```

---

## 7. The modeled hardware: DRAM organization + PIM units

### DRAM hierarchy (LPDDR5/5X, from the `.ini` configs)

```
Channel (4)
 └─ Rank (per config)
     └─ Bankgroup (4)
         └─ Bank (4 per group)
             └─ Row (e.g. 49152; subarray = 512 rows)
                 └─ Column (1024) — device_width 16 bit, BL 16
```

### PIM compute units

MAC state machine per unit (`pim_controller.h:52`):

```cpp
struct MacState {
  bool is_active; int num_macs; int remaining_macs;
  uint64_t last_mac_cycle; Address addr; uint64_t hex_addr;
};
```

Placement depends on `compute_mode`:

| `compute_mode` | MAC units | Meaning |
|---|---|---|
| `ALL_BANK` | 1 per channel | one op uses all banks in lockstep |
| `ASYNC` | one per (rank × bankgroup) | independent per-bankgroup MACs — **this is the mode your async `pim.dispatch` targets** |

Other policy knobs (in the `.ini` `[system]` section):

| Knob | Values | Effect |
|---|---|---|
| `scheduling_policy` | `PIM_FIRST` / `MEM_FIRST` | who wins arbitration between PIM commands and normal R/W (MEM_FIRST drains memory queue to `mem_first_threshold` first) |
| `bank_mode` | `SINGLE` / `DPSA` | single vs dual-subarray activation per bank |
| `pim_type` | e.g. `SINGLE` | dram_structure section |

### Address mapping

`address_mapping = rorabacobgch` in the `.ini` — fields LSB→MSB after shifting
off `log2(request_size)` alignment bits: **ch, bg, co, ba, ra, ro** (read the
string right-to-left). Decompose/compose in `src/Address.cc` and
`PIMSim/src/configuration.cc`. Consequence for you: *consecutive cache lines
interleave across channels/bankgroups* — where a `pim.dispatch` address lands
physically is decided here, which matters once tokens map to bankgroups.

---

## 8. Configuration chain (who reads what)

```
your gem5 config script
  DRAMsim3(mem_config = ".../pimony_mem.json")        ← gem5-side param
        │
        ├── mem_config json:  pim_config_path → which .ini   ┐
        │                     dram_channels, req_size, page  │ engine top layer
        │                     size, banks/bankgroups per ch  ┘
        │
        ├── that .ini (PIMSim/configs/LPDDR5X_*.ini):
        │       [dram_structure] geometry  [timing] JEDEC params
        │       [system] queues, scheduling_policy, compute_mode  [power]
        │
        └── model_config json: GPT/GEMV shape for the built-in LLM generator
```

- Working example: [configs/learning_gem5/part1/basic_pimony_system.py:92](configs/learning_gem5/part1/basic_pimony_system.py#L92)
  with [configs/scratch/pimony_mem.json](configs/scratch/pimony_mem.json).
  **Path trap:** the shipped default `pimony.json` has a `pim_config_path`
  relative to `ext/dramsim3/PIMony/`, which only resolves if you run gem5 from
  there — that's why `pimony_mem.json` exists with a gem5-root-relative path.
- `DRAMsim3` here **is** the whole memory (an `AbstractMemory` with its own
  port) — no `MemCtrl`/`.dram` split like gem5's native DRAM models.
- The stdlib component [src/python/gem5/components/memory/pimony.py](src/python/gem5/components/memory/pimony.py)
  has **hardcoded `/home/spec-2017/...` paths** — broken on this machine;
  prefer the direct `DRAMsim3(...)` instantiation.
- Runs happen inside the `gem5-container` Docker, not the WSL host.

---

## 9. End-to-end timeline of one `pim.dispatch`

```
cycle T    CPU executes pim.dispatch rd, rs1(addr), rs2(size)
           → store-like access, flags PIM_DISPATCH|UNCACHEABLE, payload=size
T+δ        DRAMsim3::recvTimingReq: sees flag
           → wrapper.enqueuePIM(addr,size) → AddMACTransaction(addr,size)
           → response packet queued THIS tick (CPU unblocked: async!)
T+δ'       CPU continues; engine has a TransactionType::MAC pending
...        every DRAM clock: gem5 tick() → ClockTick()
           PIMController arbitrates (PIM_FIRST/MEM_FIRST), issues
           CommandType::MAC to a MacState in some bankgroup (ASYNC mode);
           remaining_macs counts down under LPDDR5X timing
T+N        engine: all PIM ops done → pim_callback()
           → gem5 pimComplete() → currently exitSimLoop("PIM_DONE")
           → [YOUR THESIS WORK: instead, raise interrupt / update token
              scoreboard so pim.wait wakes up]
```

---

## 10. Cheat sheet

| I want to… | Go to |
|---|---|
| change how dispatch packets are intercepted | [pimony.cc:215](src/mem/pimony.cc#L215) |
| change what completion does (pim.wait!) | `pimComplete()` [pimony.cc:339](src/mem/pimony.cc#L339) |
| add a method gem5 can call on the engine | wrapper ([pimony_wrapper.hh](src/mem/pimony_wrapper.hh)/.cc) **and** `ext/.../src/memory_system.h/cc` |
| add a SimObject parameter | [PIMony.py](src/mem/PIMony.py) + `Params` use in pimony.cc |
| change PIM/memory arbitration or MAC timing | `ext/.../PIMSim/src/pim_controller.cc`, `pim_command_queue.cc` |
| change when `pim_callback` fires / make it per-token | `ext/.../src/memory_system.cc` (ClockTick loop) + `Request.cc` (`is_pim_operation_done`) |
| change DRAM geometry/timing/policy | the `.ini` in `ext/.../PIMSim/configs/` (pick via mem_config json) |
| debug the memory side | `--debug-flags=DRAMsim3` (yes, that name) |
| rebuild the engine | conan + cmake in `ext/dramsim3/PIMony/` (see its README); gem5 links `libdramsim3.so` |

### Known sharp edges
1. `pimComplete()` **ends the simulation** — placeholder, must be replaced for pim.wait.
2. `pim_cb` is `void()` — no token identity; scoreboard needs a widened signature through 3 layers.
3. PIM dispatches **skip the queue-full backpressure check** in `recvTimingReq`.
4. Size narrows `uint64_t → uint32_t` at `AddMACTransaction`.
5. Burst size must equal cache-line size or gem5 `fatal()`s at init.
6. Engine writes split across channel pairs internally (don't be surprised by per-channel stats).
7. Everything still answers to the name `DRAMsim3` in code, configs, and debug flags.
