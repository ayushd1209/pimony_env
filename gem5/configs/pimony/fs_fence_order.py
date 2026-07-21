# O3 ordering-demo config. Same wiring as fs_fence_test.py, but:
#   argv[1] = cpu model (default o3 — the only one that reorders)
#   argv[2] = workload binary (so one config runs both the no-fence and
#             with-fence builds)
#
# Run (inside gem5-container), both builds:
#   build/RISCV/gem5.opt --debug-flags=DRAMsim3 configs/pimony/fs_fence_order.py \
#       o3 tests/test-progs/fence_order/fence_order_nofence 2>&1 | tee m5out/order_nofence.log
#   build/RISCV/gem5.opt --debug-flags=DRAMsim3 configs/pimony/fs_fence_order.py \
#       o3 tests/test-progs/fence_order/fence_order_fence   2>&1 | tee m5out/order_fence.log

import m5
from m5.objects import *
import sys

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "1GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
system.mem_ranges = [AddrRange(start=0x80000000, size="2GB")]

PIM_REG_BASE = 0x100000000
PIM_REG_SIZE = 0x1000

_cpu_arg = sys.argv[1] if len(sys.argv) > 1 else "o3"
_bin = sys.argv[2] if len(sys.argv) > 2 \
    else "tests/test-progs/fence_order/fence_order_nofence"
_cpu_models = {
    "timing": RiscvTimingSimpleCPU,
    "minor":  RiscvMinorCPU,
    "o3":     RiscvO3CPU,
}
system.cpu = _cpu_models[_cpu_arg]()
print("CPU model:", _cpu_arg, "| workload:", _bin)

system.membus = SystemXBar()

system.cpu.icache = Cache(size="16KiB", assoc=2,
                          tag_latency=2, data_latency=2, response_latency=2,
                          mshrs=4, tgts_per_mshr=20)
system.cpu.dcache = Cache(size="16KiB", assoc=2,
                          tag_latency=2, data_latency=2, response_latency=2,
                          mshrs=4, tgts_per_mshr=20)
system.cpu.icache.cpu_side = system.cpu.icache_port
system.cpu.dcache.cpu_side = system.cpu.dcache_port
system.cpu.icache.mem_side = system.membus.cpu_side_ports
system.cpu.dcache.mem_side = system.membus.cpu_side_ports

system.cpu.mmu.connectWalkerPorts(
    system.membus.cpu_side_ports, system.membus.cpu_side_ports)

system.cpu.mmu.pma_checker = PMAChecker(
    uncacheable=[AddrRange(PIM_REG_BASE, PIM_REG_BASE + PIM_REG_SIZE)])

system.cpu.createInterruptController()
system.system_port = system.membus.cpu_side_ports

system.mem_ctrl = DRAMsim3(
    mem_config="configs/pimony/pimony_mem.json",
    model_config="ext/dramsim3/PIMony/configs/model_configs/gpt3-2.7B_single_layer.json",
)
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

system.cpu.interrupts[0].local_interrupt_ids = [8]
system.mem_ctrl.pim_int_source = system.cpu.interrupts[0].local_interrupt_pins[0]

system.workload = RiscvBareMetal()
system.workload.bootloader = _bin

system.cpu.createThreads()

root = Root(full_system=True, system=system)
m5.instantiate()

print("Beginning O3 ordering demo!")
exit_event = m5.simulate()
print("Exiting @ tick %i because %s" % (m5.curTick(), exit_event.getCause()))
