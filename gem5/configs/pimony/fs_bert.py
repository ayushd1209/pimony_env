# BERT layer baseline on the fence_e2e machine -- CPU only, no PIM calls.
# Byte-for-byte the same machine as fs_fence_e2e.py; only the workload differs.
#
# Run (inside gem5-container):
#   build/RISCV/gem5.opt --debug-flags=DRAMsim3 \
#       configs/pimony/fs_bert.py o3 2>&1 | tee m5out/bert_bm.log

import m5
from m5.objects import *
import sys

system = System()
system.clk_domain = SrcClockDomain()
system.clk_domain.clock = "3GHz"
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

# --- vector functional unit timing (argv[3] = latency, argv[4] = unit count) ---
# gem5's default gives every Simd* op opLat=1, while scalar FloatMultAcc is 5.
# argv[3] lets you make them consistent and see how much the 1-cycle vector
# assumption was worth.  Defaults reproduce gem5's stock behaviour.
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

system.cpu.icache = Cache(size="16KiB", assoc=2,
                          tag_latency=2, data_latency=2, response_latency=2,
                          mshrs=4, tgts_per_mshr=20)
system.cpu.dcache = Cache(size="16KiB", assoc=2,
                          tag_latency=2, data_latency=2, response_latency=2,
                          mshrs=16, tgts_per_mshr=20,
                          prefetcher=StridePrefetcher())
system.cpu.icache.cpu_side = system.cpu.icache_port
system.cpu.dcache.cpu_side = system.cpu.dcache_port

# L2: unified, sits below both L1s on its own bus (L2XBar), then to membus.
system.l2bus = L2XBar()
system.cpu.icache.mem_side = system.l2bus.cpu_side_ports
system.cpu.dcache.mem_side = system.l2bus.cpu_side_ports

system.l2cache = Cache(size="2MiB", assoc=8,
                       tag_latency=20, data_latency=20, response_latency=20,
                       mshrs=20, tgts_per_mshr=12,
                       prefetcher=StridePrefetcher())
system.l2cache.cpu_side = system.l2bus.mem_side_ports
system.l2cache.mem_side = system.membus.cpu_side_ports

system.cpu.mmu.connectWalkerPorts(
    system.membus.cpu_side_ports, system.membus.cpu_side_ports)

system.cpu.mmu.pma_checker = PMAChecker(
    uncacheable=[AddrRange(PIM_REG_BASE, PIM_REG_BASE + PIM_REG_SIZE)])

system.cpu.createInterruptController()
system.system_port = system.membus.cpu_side_ports

# argv[5] optionally selects the PIMony memory-policy config. The four differ in
# scheduling_policy (MEM_FIRST / PIM_FIRST), compute_mode and bank_mode -- i.e.
# who wins when the host and PIM want the memory at the same time.
_mem_arg = sys.argv[5] if len(sys.argv) > 5 else "configs/pimony/pimony_mem.json"
print("mem_config:", _mem_arg)

system.mem_ctrl = DRAMsim3(
    mem_config=_mem_arg,
    model_config="ext/dramsim3/PIMony/configs/model_configs/gpt3-2.7B_single_layer.json",
)
system.mem_ctrl.range = system.mem_ranges[0]
system.mem_ctrl.port = system.membus.mem_side_ports

system.cpu.interrupts[0].local_interrupt_ids = [8]
system.mem_ctrl.pim_int_source = system.cpu.interrupts[0].local_interrupt_pins[0]

system.workload = RiscvBareMetal()
# argv[2] optionally selects the binary; default is the vector build
_bin_arg = sys.argv[2] if len(sys.argv) > 2 else "configs/scratch/progs/bert_bm"
system.workload.bootloader = _bin_arg
print("workload:", _bin_arg)

system.cpu.createThreads()

root = Root(full_system=True, system=system)

# argv[6] = "save:<dir>" or "restore:<dir>", to skip init_params()'s 78 M weight
# generations (~90 of every 100 minutes of a run).
#
# ⚠️ BUILT AND MEASURED 2026-09-25 -- IT WORKS BUT IS NOT TRANSPARENT, SO IT IS
# NOT USED. Restore takes 15 s, but the restored run is ~10% SLOWER than the
# same binary run straight through, and does not execute the same instructions
# (5 differed in the attention block of the tiny test). gem5 checkpoints do not
# carry cache contents, so a restored run starts cold while a straight-through
# run inherits whatever init_params left warm -- the small blocks are hit
# hardest, which is the pattern observed (+122% on block 8, +39% on block 2).
#
# Using it would mean re-baselining every number in the report onto cold caches.
# Left in place, inert by default, so the next person does not rediscover this.
# Checkpoints are also PER-BINARY: they amortise config sweeps, not binary ones.
_ckpt = sys.argv[6] if len(sys.argv) > 6 else None
_mode, _ckpt_dir = (_ckpt.split(":", 1) if _ckpt else (None, None))
if _mode:
    print("checkpoint mode: %s -> %s" % (_mode, _ckpt_dir))

if _mode == "restore":
    m5.instantiate(_ckpt_dir)
else:
    m5.instantiate()

print("Beginning BERT layer!")
exit_event = m5.simulate()
print("Exiting @ tick %i because %s" % (m5.curTick(), exit_event.getCause()))

if _mode == "save":
    if exit_event.getCause() != "checkpoint":
        print("ERROR: expected a checkpoint hypercall, got %r. "
              "Was the binary built with -DTAKE_CKPT?" % exit_event.getCause())
    else:
        m5.checkpoint(_ckpt_dir)
        print("Checkpoint written to %s -- restore with 'restore:%s'"
              % (_ckpt_dir, _ckpt_dir))
elif _mode == "restore":
    # the hypercall is consumed by the checkpoint run, so a restored run
    # continues straight into m5_reset_stats() and the ROI
    pass
