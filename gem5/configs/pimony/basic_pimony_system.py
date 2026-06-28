# -*- coding: utf-8 -*-
#
# PIMony DRAM model (the `DRAMsim3` SimObject defined in src/mem/PIMony.py,
# backed by ext/dramsim3/PIMony/):
#   - single RiscvTimingSimpleCPU (in-order, timing)
#   - two-level classic cache hierarchy (L1 I/D + L2)
#   - SE mode
#
# Run from the gem5 root (inside the gem5-container):
#   build/RISCV/gem5.opt configs/pimony/basic_pimony_system.py [binary]

import m5
from m5.objects import *

# Add the common scripts to our path. Resolved relative to the main script dir
# (configs/pimony/), so one hop up reaches configs/ where `common` lives.
m5.util.addToPath("../")

# import the caches which we made (caches.py lives next to this file)
from caches import *

# import the SimpleOpts module
from common import SimpleOpts

# Default to running 'hello', use the compiled ISA to find the binary
thispath = os.path.dirname(os.path.realpath(__file__))
default_binary = os.path.join(
    thispath,
    "../../../",
    "tests/test-progs/hello/bin/riscv/linux/hello",
)

# Binary to execute
SimpleOpts.add_option("binary", nargs="?", default=default_binary)
args = SimpleOpts.parse_args()

# create the system we are going to simulate
system = System()

# Set the clock frequency of the system (and all of its children)
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

# Set up the system
system.mem_mode = "timing"  # Use timing accesses
system.mem_ranges = [AddrRange("512MB")]  # Create an address range

# Create a simple CPU
system.cpu = RiscvTimingSimpleCPU()

# Create an L1 instruction and data cache
system.cpu.icache = L1ICache(args)
system.cpu.dcache = L1DCache(args)

# Connect the instruction and data caches to the CPU
system.cpu.icache.connectCPU(system.cpu)
system.cpu.dcache.connectCPU(system.cpu)

# Create a memory bus, a coherent crossbar, in this case
system.l2bus = L2XBar()

# Hook the CPU ports up to the l2bus
system.cpu.icache.connectBus(system.l2bus)
system.cpu.dcache.connectBus(system.l2bus)

# Create an L2 cache and connect it to the l2bus
system.l2cache = L2Cache(args)
system.l2cache.connectCPUSideBus(system.l2bus)

# Create a memory bus
system.membus = SystemXBar()

# Connect the L2 cache to the membus
system.l2cache.connectMemSideBus(system.membus)

# create the interrupt controller for the CPU
system.cpu.createInterruptController()

# Connect the system up to the membus
system.system_port = system.membus.cpu_side_ports

# --- PIMony memory ---------
# DRAMsim3 here IS the PIMony model (src/mem/PIMony.py -> mem/pimony.hh).
# It is an AbstractMemory, so it owns its own `range` and `port` directly --
# no MemCtrl/.dram split. Passed a corrected mem_config
# (configs/pimony/pimony_mem.json) whose pim_config_path points at the real
# .ini location relative to the gem5 root. The shipped default (pimony.json)
# uses a path that only resolves if gem5 runs from inside ext/dramsim3/PIMony/.
# model_config's default is already root-relative, so we leave it.

# model_config = PIMony's built-in workload (gpt3 layer)
system.mem_ctrl = DRAMsim3(
    mem_config="configs/pimony/pimony_mem.json",
    # DEAD: LLM auto-run disabled (PIM trace seed/regenerate commented out in
    # Request.cc). This model config is parsed at startup but never consumed.
    # Kept only to avoid a startup parse hiccup; remove once CPU-driven PIM is verified.
    model_config="ext/dramsim3/PIMony/configs/model_configs/gpt3-2.7B_single_layer.json",
)
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

# --- PIM completion interrupt: pin-wired -----------------------------------
# On PIM completion PIMony drives its own IntSourcePin; we wire that source to
# a local-interrupt sink on the hart, the way silicon connects an interrupt
# line (no direct postInterrupt reach-in). local_interrupt_id 8 makes the hart
# build a matching sink; raiseInterruptPin adds +16, so this fires
# INT_LOCAL_8 (interrupt 24 -> mip/mie bit 24, mcause low bits 24).
system.cpu.interrupts[0].local_interrupt_ids = [8]
system.mem_ctrl.pim_int_source = system.cpu.interrupts[0].local_interrupt_pins[0]
# -------------------------------------------------------------------------

system.workload = SEWorkload.init_compatible(args.binary)

# Create a process for a simple "Hello World" application
process = Process()
process.cmd = [args.binary]
system.cpu.workload = process
system.cpu.createThreads()

# set up the root SimObject and start the simulation
root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning simulation!")
exit_event = m5.simulate()
print("Exiting @ tick %i because %s" % (m5.curTick(), exit_event.getCause()))
