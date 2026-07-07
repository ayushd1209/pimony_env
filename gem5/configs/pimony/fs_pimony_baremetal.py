# Bare-metal RISC-V config for the PIM register-block smoke test (Stage A).
#
# Boots our hand-written bare-metal ELF (tests/test-progs/pim_baremetal/) in
# M-mode from its entry point -- no OS, no OpenSBI. The program does:
#   pim.dispatch -> poll PIM_DONE (MMIO) -> W1C ack -> m5_exit.
# This validates the PIMony register block (read / write-1-to-clear / line)
# before we add the interrupt handler.
#
# Run from the gem5 root (inside gem5-container):
#   build/RISCV/gem5.opt configs/pimony/fs_pimony_baremetal.py

import m5
from m5.objects import *

system = System()

system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
# DRAM at the load/reset address (0x80000000). 512MB is plenty for a tiny
# bare-metal program; its top (0xA0000000) stays well below the PIM register
# window at 0x100000000, so the two address ranges never overlap.
system.mem_ranges = [AddrRange(start=0x80000000, size="512MB")]

system.cpu = RiscvMinorCPU()   # detailed in-order pipeline (was RiscvTimingSimpleCPU)

system.membus = SystemXBar()

# No caches: wire the CPU straight to the membus. Every load/store then goes
# to memory/device with no caching, so MMIO reads of PIM_DONE are never stale.
system.cpu.icache_port = system.membus.cpu_side_ports
system.cpu.dcache_port = system.membus.cpu_side_ports

# Page-table walker ports: the MMU reads PTEs from memory once paging is on.
# Unconnected in Bare mode is fine, but Sv39 needs these wired to the bus.
system.cpu.mmu.connectWalkerPorts(
    system.membus.cpu_side_ports, system.membus.cpu_side_ports)

system.cpu.createInterruptController()

system.system_port = system.membus.cpu_side_ports

# --- PIMony memory (same instantiation as the SE config) -------------------
system.mem_ctrl = DRAMsim3(
    mem_config="configs/pimony/pimony_mem.json",
    model_config="ext/dramsim3/PIMony/configs/model_configs/gpt3-2.7B_single_layer.json",
)
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# --- PIM completion interrupt pin (wired even for the poll test) -----------
# pimComplete() raises this pin; an unconnected IntSourcePin would fatal at
# instantiate, so we wire it the same way as the SE config (local id 8 -> cause 24).
system.cpu.interrupts[0].local_interrupt_ids = [8]
system.mem_ctrl.pim_int_source = system.cpu.interrupts[0].local_interrupt_pins[0]

# --- Bare-metal workload: load our ELF, reset PC to its entry point (M-mode) -
system.workload = RiscvBareMetal()
system.workload.bootloader = "tests/test-progs/pim_baremetal/pim_baremetal"

system.cpu.createThreads()

root = Root(full_system=True, system=system)
m5.instantiate()

print("Beginning bare-metal PIM simulation!")
exit_event = m5.simulate()
print("Exiting @ tick %i because %s" % (m5.curTick(), exit_event.getCause()))
