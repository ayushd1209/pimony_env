# Bare-metal RISC-V config WITH L1 caches — for bringing up a pipelined host
# (MinorCPU / O3) on the PIM platform.
#
# Why caches: a pipelined CPU fires memory traffic rapidly/overlapping straight
# at PIMony in the cacheless config, which drives PIMony's DRAM model into a bad
# state (phantom read completion -> assert). L1 caches absorb most accesses so
# only a small, regular stream of cache-line misses reaches PIMony. Caches are
# also the realistic config for a pipelined host.
#
# MMIO correctness: with a cache, plain loads of PIM_DONE could return a stale
# cached copy. So the PIM register window is marked UNCACHEABLE via PMAChecker.
# (pim.dispatch already carries a per-instruction UNCACHEABLE flag in the ISA,
#  so the dispatch store bypasses the cache and reaches PIMony — unchanged.)
#
# Run (inside gem5-container):
#   build/RISCV/gem5.opt configs/pimony/fs_pimony_baremetal_cached.py

import m5
from m5.objects import *

system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
system.mem_ranges = [AddrRange(start=0x80000000, size="512MB")]

# PIM MMIO register window (above DRAM). Must match pim_reg_base / main.c.
PIM_REG_BASE = 0x100000000
PIM_REG_SIZE = 0x1000

# CPU model selectable via command-line arg (default minor):
#   ... fs_pimony_baremetal_cached.py timing | minor | o3
import sys
_cpu_arg = sys.argv[1] if len(sys.argv) > 1 else "minor"
_cpu_models = {
    "timing": RiscvTimingSimpleCPU,   # in-order, non-pipelined
    "minor":  RiscvMinorCPU,          # detailed in-order pipeline
    "o3":     RiscvO3CPU,             # out-of-order
}
system.cpu = _cpu_models[_cpu_arg]()
print("CPU model:", _cpu_arg)

system.membus = SystemXBar()

# --- Step 2: L1 caches between the CPU and the membus ----------------------
system.cpu.icache = Cache(size="16KiB", assoc=2,
                          tag_latency=2, data_latency=2, response_latency=2,
                          mshrs=4, tgts_per_mshr=20)
system.cpu.dcache = Cache(size="16KiB", assoc=2,
                          tag_latency=2, data_latency=2, response_latency=2,
                          mshrs=4, tgts_per_mshr=20)

# --- Step 1: topology — CPU port -> cache cpu_side ; cache mem_side -> membus
system.cpu.icache.cpu_side = system.cpu.icache_port
system.cpu.dcache.cpu_side = system.cpu.dcache_port
system.cpu.icache.mem_side = system.membus.cpu_side_ports
system.cpu.dcache.mem_side = system.membus.cpu_side_ports

# Page-table walker ports go straight to the membus (small PTE traffic).
system.cpu.mmu.connectWalkerPorts(
    system.membus.cpu_side_ports, system.membus.cpu_side_ports)

# --- Step 3: mark the PIM MMIO window uncacheable so PIM_DONE isn't stale ---
system.cpu.mmu.pma_checker = PMAChecker(
    uncacheable=[AddrRange(PIM_REG_BASE, PIM_REG_BASE + PIM_REG_SIZE)])

system.cpu.createInterruptController()

system.system_port = system.membus.cpu_side_ports

# --- PIMony memory ---------------------------------------------------------
system.mem_ctrl = DRAMsim3(
    mem_config="configs/pimony/pimony_mem.json",
    model_config="ext/dramsim3/PIMony/configs/model_configs/gpt3-2.7B_single_layer.json",
)
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# --- PIM completion interrupt pin (local id 8 -> cause 24) -----------------
system.cpu.interrupts[0].local_interrupt_ids = [8]
system.mem_ctrl.pim_int_source = system.cpu.interrupts[0].local_interrupt_pins[0]

# --- Bare-metal workload ---------------------------------------------------
system.workload = RiscvBareMetal()
system.workload.bootloader = "tests/test-progs/pim_baremetal/pim_baremetal"

system.cpu.createThreads()

root = Root(full_system=True, system=system)
m5.instantiate()

print("Beginning bare-metal PIM simulation (cached)!")
exit_event = m5.simulate()
print("Exiting @ tick %i because %s" % (m5.curTick(), exit_event.getCause()))
