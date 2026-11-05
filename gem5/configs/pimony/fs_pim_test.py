# skip: plumbing. Same machine as fs_pim_gemv.py, for any bare-metal test binary.
# argv[1] = cpu (timing | minor | o3), argv[2] = binary. The exit line also gives the
# m5_fail code, so a failing test says which check failed.
#
# Run (inside gem5-container):
#   build/RISCV/gem5.opt -d m5out/t --debug-flags=DRAMsim3 --debug-file=dbg.log \
#       configs/pimony/fs_pim_test.py o3 tests/test-progs/pim_wrongpath/pim_wrongpath
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

_cpu_arg = sys.argv[1] if len(sys.argv) > 1 else "timing"
_cpu_models = {
    "timing": RiscvTimingSimpleCPU,
    "minor":  RiscvMinorCPU,
    "o3":     RiscvO3CPU,
}
system.cpu = _cpu_models[_cpu_arg]()
print("CPU model:", _cpu_arg)

system.membus = SystemXBar()

system.cpu.icache = Cache(size="16KiB", assoc=2,
                          tag_latency=2, data_latency=2, response_latency=2,
                          mshrs=4, tgts_per_mshr=20)
system.cpu.dcache = Cache(size="16KiB", assoc=2,
                          tag_latency=2, data_latency=2, response_latency=2,
                          mshrs=4, tgts_per_mshr=20)
system.cpu.icache.cpu_side = system.cpu.icache_port
system.cpu.dcache.cpu_side = system.cpu.dcache_port

system.l2bus = L2XBar()
system.cpu.icache.mem_side = system.l2bus.cpu_side_ports
system.cpu.dcache.mem_side = system.l2bus.cpu_side_ports

system.l2cache = Cache(size="256KiB", assoc=8,
                       tag_latency=20, data_latency=20, response_latency=20,
                       mshrs=20, tgts_per_mshr=12)
system.l2cache.cpu_side = system.l2bus.mem_side_ports
system.l2cache.mem_side = system.membus.cpu_side_ports

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

if len(sys.argv) < 3:
    sys.exit("usage: fs_pim_test.py <timing|minor|o3> <binary>")
system.workload = RiscvBareMetal()
system.workload.bootloader = sys.argv[2]
print("bootloader:", system.workload.bootloader)

system.cpu.createThreads()

root = Root(full_system=True, system=system)
m5.instantiate()

print("Beginning PIM test:", sys.argv[2])
exit_event = m5.simulate()
print("Exiting @ tick %i because %s (code %d)" % (m5.curTick(), exit_event.getCause(),
                                                exit_event.getCode()))
