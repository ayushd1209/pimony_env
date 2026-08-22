"""
Run the BERT layer on a vector-enabled RISC-V CPU in SE mode.

  gem5.opt [--outdir=DIR] configs/scratch/bert_vector.py [options] BINARY

Options:
  --cpu {o3,timing,atomic}   CPU model (default o3; only o3 models issue width
                             and functional units, so only o3 gives a
                             meaningful scalar-vs-vector comparison)
  --vlen N                   bits per vector register (default 256)
  --elen N                   widest vector element in bits (default 64)
  --simd-lat N               cycles per vector op (default 1 = gem5's own
                             default, i.e. an infinitely wide free vector unit)
  --simd-units N             number of vector functional units (default 4)
  --simd-unpipelined         occupy the unit for --simd-lat cycles instead of
                             pipelining; this is what models a narrow unit

To approximate an L-lane fp32 vector unit at VLEN=256, one register's worth of
work takes 256/32/L cycles of occupancy:
  2 lanes:  --simd-lat 4 --simd-units 1 --simd-unpipelined
  4 lanes:  --simd-lat 2 --simd-units 1 --simd-unpipelined
  8 lanes:  --simd-lat 1 --simd-units 1 --simd-unpipelined
"""

import argparse

from m5.objects import (
    FUDesc,
    FUPool,
    FP_ALU,
    FP_MultDiv,
    IntALU,
    IntMultDiv,
    IQUnit,
    Matrix_Unit,
    OpDesc,
    PredALU,
    RdWrPort,
    ReadPort,
    RiscvAtomicSimpleCPU,
    RiscvO3CPU,
    RiscvTimingSimpleCPU,
    System_Unit,
    WritePort,
)

from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_cache_hierarchy import (
    PrivateL1PrivateL2CacheHierarchy,
)
from gem5.components.memory.single_channel import SingleChannelDDR4_2400
from gem5.components.processors.base_cpu_core import BaseCPUCore
from gem5.components.processors.base_cpu_processor import BaseCPUProcessor
from gem5.isas import ISA
from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import Simulator

# every op class SIMD_Unit covers -- all of them must stay covered or an
# instruction with a missing op class would have no unit to issue to
SIMD_OPS = [
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

parser = argparse.ArgumentParser()
parser.add_argument("binary")
parser.add_argument("--cpu", choices=["o3", "timing", "atomic"], default="o3")
parser.add_argument("--vlen", type=int, default=256)
parser.add_argument("--elen", type=int, default=64)
parser.add_argument("--simd-lat", type=int, default=1)
parser.add_argument("--simd-units", type=int, default=4)
parser.add_argument("--simd-unpipelined", action="store_true")
parser.add_argument("--l1d", default="32KiB")
parser.add_argument("--l1i", default="32KiB")
parser.add_argument("--l2", default="512KiB")
parser.add_argument("--clk", default="3GHz")
args = parser.parse_args()

CPU_CLASS = {
    "o3": RiscvO3CPU,
    "timing": RiscvTimingSimpleCPU,
    "atomic": RiscvAtomicSimpleCPU,
}[args.cpu]


def simd_pool():
    """DefaultFUPool with the vector unit's latency/count/pipelining swapped."""
    simd = FUDesc(
        opList=[
            OpDesc(
                opClass=op,
                opLat=args.simd_lat,
                pipelined=not args.simd_unpipelined,
            )
            for op in SIMD_OPS
        ],
        count=args.simd_units,
    )
    return FUPool(
        FUList=[
            IntALU(), IntMultDiv(), FP_ALU(), FP_MultDiv(), ReadPort(),
            simd, Matrix_Unit(), System_Unit(), PredALU(), WritePort(),
            RdWrPort(),
        ]
    )


class VectorCore(BaseCPUCore):
    def __init__(self):
        super().__init__(core=CPU_CLASS(cpu_id=0), isa=ISA.RISCV)
        self.core.isa[0].enable_rvv = True
        self.core.isa[0].vlen = args.vlen
        self.core.isa[0].elen = args.elen
        # functional units only exist on O3; replace the IQ's pool wholesale
        # rather than mutating the shared default (cf. O3_ARM_v7a.py)
        if args.cpu == "o3":
            self.core.instQueues = [IQUnit(fuPool=simd_pool())]


board = SimpleBoard(
    clk_freq=args.clk,
    processor=BaseCPUProcessor(cores=[VectorCore()]),
    memory=SingleChannelDDR4_2400("2GiB"),
    cache_hierarchy=PrivateL1PrivateL2CacheHierarchy(
        l1d_size=args.l1d, l1i_size=args.l1i, l2_size=args.l2
    ),
)

board.set_se_binary_workload(BinaryResource(local_path=args.binary))

print(
    f"cpu={args.cpu} vlen={args.vlen} elen={args.elen} "
    f"simd_lat={args.simd_lat} simd_units={args.simd_units} "
    f"pipelined={not args.simd_unpipelined}"
)
Simulator(board=board).run()
