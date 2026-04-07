
from pathlib import Path
import argparse
import time
import os
import json

import m5
from m5.objects import Root

from gem5.utils.requires import requires
from gem5.components.boards.x86_board import X86Board
from gem5.components.memory import PIMony_SingleChannelLPDDR
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.components.processors.cpu_types import CPUTypes
from gem5.isas import ISA
from gem5.coherence_protocol import CoherenceProtocol
from gem5.resources.resource import Resource, CustomDiskImageResource
from gem5.simulate.simulator import Simulator
from gem5.simulate.exit_event import ExitEvent

from m5.stats.gem5stats import get_simstat
from m5.util import warn
from m5.util import fatal

# We check for the required gem5 build.


requires(
    isa_required=ISA.X86,
    coherence_protocol_required=CoherenceProtocol.MESI_THREE_LEVEL,
    kvm_required=True,
)

benchmark_choices = [
    "500.perlbench_r",
    "502.gcc_r",
    "503.bwaves_r",
    "505.mcf_r",
    "507.cactuBSSN_r",
    "508.namd_r",
    "510.parest_r",
    "511.povray_r",
    "519.lbm_r",
    "520.omnetpp_r",
    "521.wrf_r",
    "523.xalancbmk_r",
    "525.x264_r",
    "526.blender_r",
    "527.cam4_r",
    "531.deepsjeng_r",
    "538.imagick_r",
    "541.leela_r",
    "544.nab_r",
    "548.exchange2_r",
    "549.fotonik3d_r",
    "554.roms_r",
    "557.xz_r",
    "600.perlbench_s",
    "602.gcc_s",
    "603.bwaves_s",
    "605.mcf_s",
    "607.cactusBSSN_s",
    "608.namd_s",
    "610.parest_s",
    "611.povray_s",
    "619.lbm_s",
    "620.omnetpp_s",
    "621.wrf_s",
    "623.xalancbmk_s",
    "625.x264_s",
    "627.cam4_s",
    "628.pop2_s",
    "631.deepsjeng_s",
    "638.imagick_s",
    "641.leela_s",
    "644.nab_s",
    "648.exchange2_s",
    "649.fotonik3d_s",
    "654.roms_s",
    "657.xz_s",
    "996.specrand_fs",
    "997.specrand_fr",
    "998.specrand_is",
    "999.specrand_ir",
]

# Following are the input size.

size_choices = ["test", "train", "ref"]

log_level_choices = ["debug", "info", "off"]

parser = argparse.ArgumentParser(
    description="An example configuration script to run the \
        SPEC CPU2017 benchmarks."
)

parser.add_argument(
    "--mem_config",
    type=str,
    required=True,
    help="Input the pimony memory config path",
)

parser.add_argument(
    "--model_config",
    type=str,
    required=True,
    help="Input the pimony model config path",
)

parser.add_argument(
    "--log_level",
    type=str,
    required=True,
    help="Input the pimony log level {debug, info, off}",
    choices=log_level_choices,
)

parser.add_argument(
    "--image",
    type=str,
    required=True,
    help="Input the full path to the built spec-2017 disk-image.",
)

parser.add_argument(
    "--partition",
    type=str,
    required=False,
    default=None,
    help='Input the root partition of the SPEC disk-image. If the disk is \
    not partitioned, then pass "".',
)

parser.add_argument(
    "--benchmark",
    type=str,
    required=True,
    help="Input the benchmark program to execute.",
    choices=benchmark_choices,
)

parser.add_argument(
    "--size",
    type=str,
    required=True,
    help="Sumulation size the benchmark program.",
    choices=size_choices,
)

parser.add_argument(
    "--checkpoint",
    type=str,
    required=True,
    help="Path to the checkpoint directory created by m5.checkpoint.",
)

args = parser.parse_args()

# We expect the user to input the full path of the disk-image.
if args.image[0] != "/":
    # We need to get the absolute path to this file. We assume that the file is
    # present on the current working directory.
    args.image = os.path.abspath(args.image)

if not os.path.exists(args.image):
    warn("Disk image not found!")
    print("Instructions on building the disk image can be found at: ")
    print(
        "https://gem5art.readthedocs.io/en/latest/tutorials/spec-tutorial.html"
    )
    fatal(f"The disk-image is not found at {args.image}")


if args.mem_config[0] != "/":
    # We need to get the absolute path to this file. We assume that the file is
    # present on the current working directory.
    args.mem_config = os.path.abspath(args.mem_config)

if not os.path.exists(args.mem_config):
    warn("memory config not found!")
    fatal(f"The mem_config is not found at {args.mem_config}")

if args.model_config[0] != "/":
    # We need to get the absolute path to this file. We assume that the file is
    # present on the current working directory.
    args.model_config = os.path.abspath(args.model_config)

if not os.path.exists(args.model_config):
    warn("model config not found!")
    fatal(f"The model_config is not found at {args.model_config}")

# After args = parser.parse_args()
if args.checkpoint[0] != "/":
    args.checkpoint = os.path.abspath(args.checkpoint)
if not os.path.isdir(args.checkpoint):
    fatal(f"Checkpoint directory not found: {args.checkpoint}")
ckpt_path = Path(args.checkpoint)
print(f"[host] Restoring from checkpoint: {ckpt_path}")

# Setting up all the fixed system parameters here

from gem5.components.cachehierarchies.ruby.mesi_three_level_cache_hierarchy import (
    MESIThreeLevelCacheHierarchy,
)

cache_hierarchy = MESIThreeLevelCacheHierarchy(
    l1d_size="48kB",
    l1d_assoc=8,
    l1i_size="64kB",
    l1i_assoc=8,
    l2_size="2MB",
    l2_assoc=8,
    l3_size="12MB",
    l3_assoc=16,
    num_l3_banks=1,
)

# The X86 board only supports 3 GB of main memory.
memory = PIMony_SingleChannelLPDDR("3072MB", args.mem_config, args.model_config, args.log_level)

processor = SimpleProcessor(
    cpu_type=CPUTypes.O3,
    isa=ISA.X86,
    num_cores=12,
)

# Here we setup the board. The X86Board allows for Full-System X86 simulations

board = X86Board(
    clk_freq="4.2GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

output_dir = "speclogs"
output_dir = output_dir.replace(":", "")

# We create this folder if it is absent.
try:
    os.makedirs(os.path.join(m5.options.outdir, output_dir))
except FileExistsError:
    warn("output directory already exists!")


command = f"{args.benchmark} {args.size} {output_dir}"


board.set_kernel_disk_workload(
    kernel=Resource("x86-linux-kernel-4.19.83"),
    # The location of the x86 SPEC CPU 2017 image
    disk_image=CustomDiskImageResource(
        args.image, root_partition=args.partition
    ),
    # readfile_contents=command,
    checkpoint=ckpt_path,
)


def handle_exit():
    print("Dump stats at the end of the ROI!")
    m5.stats.dump()
    yield True  # Stop the simulation. We're done.

simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.EXIT: handle_exit(),
    },
)

# We maintain the wall clock time.

globalStart = time.time()

print("Running the simulation")
print("Using TIMING cpu")

m5.stats.reset()

# We start the simulation
simulator.run()

# We print the final simulation statistics.

print("Done with the simulation")
print()
print("Performance statistics:")

roi_begin_ticks = simulator.get_tick_stopwatch()[0][1]
roi_end_ticks = simulator.get_tick_stopwatch()[1][1]

print("roi simulated ticks: " + str(roi_end_ticks - roi_begin_ticks))

print(
    "Ran a total of", simulator.get_current_tick() / 1e12, "simulated seconds"
)
print(
    "Total wallclock time: %.2fs, %.2f min"
    % (time.time() - globalStart, (time.time() - globalStart) / 60)
)
