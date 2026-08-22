# SE-mode twin of fs_bert.py. Same CPU, same caches, same DRAMsim3/PIMony
# memory and the same address base -- only the workload interface differs:
# syscall emulation instead of bare metal, so the binary may link libc and
# OpenBLAS.
#
# No PIM here. SE mode has no privileged modes, no MMIO and no local
# interrupts, so pim.dispatch/pim.wait completion cannot work; this config
# exists ONLY to compare CPU-side kernels against each other. PIM numbers stay
# in fs_*.py.
#
# Run (inside gem5-container):
#   build/RISCV/gem5.opt --outdir=m5out/se-blas configs/pimony/se_bert.py \
#       o3 configs/scratch/progs/bert_se_blas

import m5
from m5.objects import *
import sys

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "3GHz"
system.clk_domain.voltage_domain = VoltageDomain()

system.mem_mode = "timing"
# Same base as fs_bert.py -- keeps the DRAMsim3 channel/bank interleave
# identical, so cycle counts are comparable across the two configs.
system.mem_ranges = [AddrRange(start=0x80000000, size="2GB")]

_cpu_arg = sys.argv[1] if len(sys.argv) > 1 else "timing"
_cpu_models = {
    "timing": RiscvTimingSimpleCPU,
    "minor":  RiscvMinorCPU,
    "o3":     RiscvO3CPU,
}
system.cpu = _cpu_models[_cpu_arg]()
print("CPU model:", _cpu_arg)

# --- vector functional unit timing (argv[3] = latency, argv[4] = unit count) ---
# Identical to fs_bert.py so the two configs sweep the same knob.
_simd_lat   = int(sys.argv[3]) if len(sys.argv) > 3 else 1
_simd_units = int(sys.argv[4]) if len(sys.argv) > 4 else 4

_SIMD_OPS = [
    "SimdAdd", "SimdAddAcc", "SimdAlu", "SimdCmp", "SimdCvt", "SimdMisc",
    "SimdMult", "SimdMultAcc", "SimdMatMultAcc", "SimdShift", "SimdShiftAcc",
    "SimdDiv", "SimdSqrt", "SimdFloatAdd", "SimdFloatAlu", "SimdFloatCmp",
    "SimdFloatCvt", "SimdFloatDiv", "SimdFloatMisc", "SimdFloatMult",
    "SimdFloatMultAcc", "SimdFloatMatMultAcc", "SimdFloatSqrt",
    "SimdReduceAdd", "SimdReduceAlu", "SimdReduceCmp", "SimdFloatReduceAdd",
    "SimdFloatReduceCmp", "SimdExt", "SimdFloatExt", "SimdConfig",
    "SimdDotProd", "SimdAes", "SimdAesMix", "SimdSha1Hash", "SimdSha1Hash2",
    "SimdSha256Hash", "SimdSha256Hash2", "SimdShaSigma2", "SimdShaSigma3",
    "SimdSha3", "SimdSm4e", "SimdCrc", "SimdBf16Add", "SimdBf16Cmp",
    "SimdBf16Cvt", "SimdBf16DotProd", "SimdBf16MatMultAcc", "SimdBf16Mult",
    "SimdBf16MultAcc",
]

if _cpu_arg == "o3" and (_simd_lat != 1 or _simd_units != 4):
    _simd = FUDesc(
        opList=[OpDesc(opClass=op, opLat=_simd_lat) for op in _SIMD_OPS],
        count=_simd_units,
    )
    _pool = FUPool(FUList=[
        IntALU(), IntMultDiv(), FP_ALU(), FP_MultDiv(), ReadPort(),
        _simd, Matrix_Unit(), System_Unit(), PredALU(), WritePort(), RdWrPort(),
    ])
    system.cpu.instQueues = [IQUnit(fuPool=_pool)]
    print("vector FU: opLat=%d units=%d" % (_simd_lat, _simd_units))
else:
    print("vector FU: gem5 defaults (opLat=1, units=4)")

system.membus = SystemXBar()

# Cache hierarchy: byte-for-byte fs_bert.py's tuned configuration.
system.cpu.icache = Cache(size="16KiB", assoc=2,
                          tag_latency=2, data_latency=2, response_latency=2,
                          mshrs=4, tgts_per_mshr=20)
system.cpu.dcache = Cache(size="16KiB", assoc=2,
                          tag_latency=2, data_latency=2, response_latency=2,
                          mshrs=16, tgts_per_mshr=20,
                          prefetcher=StridePrefetcher())
system.cpu.icache.cpu_side = system.cpu.icache_port
system.cpu.dcache.cpu_side = system.cpu.dcache_port

system.l2bus = L2XBar()
system.cpu.icache.mem_side = system.l2bus.cpu_side_ports
system.cpu.dcache.mem_side = system.l2bus.cpu_side_ports

system.l2cache = Cache(size="256KiB", assoc=8,
                       tag_latency=20, data_latency=20, response_latency=20,
                       mshrs=20, tgts_per_mshr=12,
                       prefetcher=StridePrefetcher())
system.l2cache.cpu_side = system.l2bus.mem_side_ports
system.l2cache.mem_side = system.membus.cpu_side_ports

system.cpu.mmu.connectWalkerPorts(
    system.membus.cpu_side_ports, system.membus.cpu_side_ports)

system.cpu.createInterruptController()
system.system_port = system.membus.cpu_side_ports

system.mem_ctrl = DRAMsim3(
    mem_config="configs/pimony/pimony_mem.json",
    model_config="ext/dramsim3/PIMony/configs/model_configs/gpt3-2.7B_single_layer.json",
)
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports
# pim_int_source deliberately left unwired -- it is a VectorPort, so gem5
# accepts zero connections. No PIM completion path exists in SE mode.

_bin_arg = sys.argv[2] if len(sys.argv) > 2 else "configs/scratch/progs/bert_se"
print("workload:", _bin_arg)

system.workload = SEWorkload.init_compatible(_bin_arg)

process = Process()
process.cmd = [_bin_arg]
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()

print("Beginning BERT layer (SE mode, CPU only)!")
exit_event = m5.simulate()
print("Exiting @ tick %i because %s" % (m5.curTick(), exit_event.getCause()))
