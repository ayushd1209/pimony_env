import argparse
import time
import os
import json

import m5
from m5.objects import Root

from gem5.utils.requires import requires
from gem5.components.boards.x86_board import X86Board
from gem5.components.memory import DualChannelDDR4_2400
# from gem5.components.memory import DRAMSim3_SingleChannelDDR4_2400
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
    # coherence_protocol_required=CoherenceProtocol.MESI_TWO_LEVEL,
    coherence_protocol_required=CoherenceProtocol.MESI_THREE_LEVEL,
    kvm_required=True,
)

parser = argparse.ArgumentParser(
    description="An example configuration script to run the \
        SPEC CPU2017 benchmarks."
)

# The arguments accepted are: a. disk-image name, b. benchmark name, c.
# simulation size, and, d. root partition.

# root partition is set to 1 by default.

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
memory = DualChannelDDR4_2400(size="3GB")

processor = SimpleProcessor(
    cpu_type=CPUTypes.KVM,
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


# output_dir = "speclogs_" + "".join(x.strip() for x in time.asctime().split())
output_dir = "speclogs"
output_dir = output_dir.replace(":", "")

copies = 12
MAX_CKPTS = 20

payload = f"""#!/bin/sh
cd /home/gem5/spec2017

# echo "Reset stats (host may ignore or reset)"
# m5 exit

runcpu --size {args.size} --iterations 1 \
        --config myconfig.x86.cfg --define gcc_dir="/usr" \
        --copies {copies}\
        --noreportable --nobuild {args.benchmark} &
BENCH_PID=$!

MAX_CKPTS={MAX_CKPTS}
count=0

# warming up
sleep 50

while kill -0 "$BENCH_PID" 2>/dev/null; do
    if [ "$count" -ge "$MAX_CKPTS" ]; then
        echo "[guest] reached MAX_CKPTS=$MAX_CKPTS, stop checkpointing"
        break
    fi

    m5 exit
    
    count=$((count + 1))
    sleep 10
    
done

# wait $BENCH_PID

for filepath in /home/gem5/spec2017/result/*; do
    filename=$(basename $filepath)
    m5 writefile $filepath {output_dir}/$filename
done

sleep 20
m5 fail 0
"""

# We create this folder if it is absent.
try:
    os.makedirs(os.path.join(m5.options.outdir, output_dir))
except FileExistsError:
    warn("output directory already exists!")

board.set_kernel_disk_workload(
    kernel=Resource("x86-linux-kernel-4.19.83"),
    disk_image=CustomDiskImageResource(
        args.image, root_partition=args.partition
    ),
    readfile_contents = payload,
)

ckpt_root = os.path.join(m5.options.outdir, output_dir)
os.makedirs(ckpt_root, exist_ok=True)

_ckpt_idx = {"i": 0}

def handle_exit_periodic():
    while True:
        i = _ckpt_idx["i"]
        if i >= MAX_CKPTS:
            print(f"[host] Reached MAX_CKPTS={MAX_CKPTS}. Stopping simulation.")
            yield True  

        dest = os.path.join(ckpt_root, f"ff_{i:06d}")
        print(f"[host] CHECKPOINT -> {dest}")
        m5.checkpoint(dest)

        _ckpt_idx["i"] = i + 1
        yield False  

def handle_final_fail():
    print("[host] FINAL FAIL received. Terminating simulation.")
    return  

simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.EXIT: handle_exit_periodic(),
        ExitEvent.FAIL: handle_final_fail(),
    },
)

# We maintain the wall clock time.

globalStart = time.time()

print("Running the simulation")

m5.stats.reset()

print("running with KVM to take checkpoints...")

simulator.run()

print("Checkpoint taken. Exit.")
exit(0)


