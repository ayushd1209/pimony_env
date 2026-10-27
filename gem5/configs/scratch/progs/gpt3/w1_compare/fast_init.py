"""Execute W1 initialization on Atomic, then warm up and measure on O3.

Run from the gem5 directory:
  build/RISCV/gem5.opt --outdir=m5out/w1-fast \
      configs/scratch/progs/gpt3/w1_compare/fast_init.py cpu BINARY

The binary must issue one m5_work_begin after initializing its data and before
its warmup/cache scrub. No memory image or initialization stores are skipped.
The measured machine matches the default se_bert.py / fs_bert.py configuration.
"""

import sys

import m5
from m5.objects import *


if len(sys.argv) != 3 or sys.argv[1] not in ("cpu", "pim"):
    raise SystemExit("usage: fast_init.py [cpu|pim] BINARY")

backend, binary = sys.argv[1:]
full_system = backend == "pim"
PIM_REG_BASE = 0x100000000
PIM_REG_SIZE = 0x1000

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "3GHz"
system.clk_domain.voltage_domain = VoltageDomain()
system.mem_mode = "atomic"
system.mem_ranges = [AddrRange(start=0x80000000, size="2GB")]
system.exit_on_work_items = True

system.cpu = RiscvAtomicSimpleCPU(cpu_id=0, width=64)
system.switch_cpu = RiscvO3CPU(cpu_id=0, switched_out=True)
for cpu in (system.cpu, system.switch_cpu):
    cpu.clk_domain = system.clk_domain

system.membus = SystemXBar()
system.cpu.icache = Cache(
    size="16KiB", assoc=2,
    tag_latency=2, data_latency=2, response_latency=2,
    mshrs=4, tgts_per_mshr=20,
)
system.cpu.dcache = Cache(
    size="16KiB", assoc=2,
    tag_latency=2, data_latency=2, response_latency=2,
    mshrs=16, tgts_per_mshr=20,
    prefetcher=StridePrefetcher(),
)
system.cpu.icache.cpu_side = system.cpu.icache_port
system.cpu.dcache.cpu_side = system.cpu.dcache_port

system.l2bus = L2XBar()
system.cpu.icache.mem_side = system.l2bus.cpu_side_ports
system.cpu.dcache.mem_side = system.l2bus.cpu_side_ports
system.l2cache = Cache(
    size="2MiB", assoc=8,
    tag_latency=20, data_latency=20, response_latency=20,
    mshrs=20, tgts_per_mshr=12,
    prefetcher=StridePrefetcher(),
)
system.l2cache.cpu_side = system.l2bus.mem_side_ports
system.l2cache.mem_side = system.membus.cpu_side_ports
system.cpu.mmu.connectWalkerPorts(
    system.membus.cpu_side_ports, system.membus.cpu_side_ports
)

if full_system:
    for cpu in (system.cpu, system.switch_cpu):
        cpu.mmu.pma_checker = PMAChecker(
            uncacheable=[AddrRange(PIM_REG_BASE, PIM_REG_BASE + PIM_REG_SIZE)]
        )

    # RISC-V copies PMP CSR values without rebuilding the MMU's decoded PMP
    # table during a CPU handover. Keep this architectural state in one shared
    # object so the guest's executed PMP setup remains effective after switch.
    system.hart_pmp = PMP()
    for cpu in (system.cpu, system.switch_cpu):
        cpu.mmu.pmp = system.hart_pmp
        for tlb in (cpu.mmu.itb, cpu.mmu.dtb):
            tlb.pmp = system.hart_pmp
            tlb.walker.pmp = system.hart_pmp

system.cpu.createInterruptController()
system.system_port = system.membus.cpu_side_ports
system.mem_ctrl = DRAMsim3(
    mem_config="configs/pimony/pimony_mem.json",
    model_config="ext/dramsim3/PIMony/configs/model_configs/gpt3-2.7B_single_layer.json",
)
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

if full_system:
    system.cpu.interrupts[0].local_interrupt_ids = [8]
    system.mem_ctrl.pim_int_source = system.cpu.interrupts[0].local_interrupt_pins[0]
    system.workload = RiscvBareMetal()
    system.workload.bootloader = binary
else:
    system.workload = SEWorkload.init_compatible(binary)
    process = Process()
    process.cmd = [binary]
    system.cpu.workload = process
    system.switch_cpu.workload = process

system.cpu.createThreads()
# Use the same architectural ISA state, as gem5's standard CPU-switch config
# does. This also keeps the PIM token allocator/scoreboard attached to the hart.
system.switch_cpu.isa = system.cpu.isa
system.switch_cpu.createThreads()

root = Root(full_system=full_system, system=system)
m5.instantiate()

print("W1 fast initialization: backend=%s workload=%s" % (backend, binary))
print("Initialization CPU: Atomic; measured CPU: O3 (3GHz, default vector FU)")
print("mem_config: configs/pimony/pimony_mem.json")
exit_event = m5.simulate()
if exit_event.getCause() != "workbegin":
    raise RuntimeError(
        "Expected initialization workbegin marker, got %r at tick %d"
        % (exit_event.getCause(), m5.curTick())
    )

print("Initialization complete @ tick %d; switching to O3 before warmup/scrub"
      % m5.curTick())
# This drains memory operations, changes atomic -> timing, and transfers CPU
# ports, thread state, and the wired interrupt controller. Caches stay in place.
m5.switchCpus(system, [(system.cpu, system.switch_cpu)])
print("O3 active; timed statistics are system.switch_cpu.*")
exit_event = m5.simulate()
print("Exiting @ tick %i because %s" % (m5.curTick(), exit_event.getCause()))
if exit_event.getCause() == "workbegin":
    raise RuntimeError("Unexpected second initialization workbegin marker")
