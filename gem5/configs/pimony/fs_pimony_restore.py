# Restore the "fully booted" checkpoint and run the PIM test binary under the
# detailed (TIMING) CPU -- the actual measured region.
#
# No boot happens here. gem5 loads ./pim_fullboot_ckpt/ (made by
# fs_pimony_checkpoint.py) and resumes right where after_boot.sh started, then:
#   after_boot.sh -> `m5 readfile` -> runs /root/pim_dispatch_wait_test
#   -> hypercall 3 (after_boot.sh finished) stops the sim (stdlib default).
#
# The binary command comes from readfile_contents BELOW (read fresh on restore),
# which is why the checkpoint itself stays generic/reusable.
#
#   build/RISCV/gem5.opt configs/pimony/fs_pimony_restore.py
#   # guest stdout (the binary's printf) -> m5out/board.platform.terminal

from pathlib import Path

from gem5.components.boards.riscv_board import RiscvBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_walk_cache_hierarchy import (
    PrivateL1PrivateL2WalkCacheHierarchy,
)
from gem5.components.memory.pimony import PIMony_SingleChannelLPDDR
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import obtain_resource, CustomDiskImageResource
from gem5.simulate.simulator import Simulator

# Must match what fs_pimony_checkpoint.py wrote.
CHECKPOINT_DIR = "pim_fullboot_ckpt"

cache_hierarchy = PrivateL1PrivateL2WalkCacheHierarchy(
    l1d_size="16KiB", l1i_size="16KiB", l2_size="256KiB"
)

# Memory/cache/board MUST match the checkpoint script. ONLY the CPU type differs.
memory = PIMony_SingleChannelLPDDR(
    "3GiB",
    "configs/pimony/pimony_mem.json",
    "ext/dramsim3/PIMony/configs/model_configs/gpt3-2.7B_single_layer.json",
    "off",
)

# TIMING = detailed CPU. This is the measured region. pim.wait completes here
# (PIM is inert under ATOMIC, so the boot CPU could never run the ROI anyway).
processor = SimpleProcessor(
    cpu_type=CPUTypes.TIMING, isa=ISA.RISCV, num_cores=1
)

board = RiscvBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# Same disk + the binary command. readfile_contents IS read here (after restore,
# after_boot.sh calls `m5 readfile`) -> this is what actually launches the binary.
board.set_kernel_disk_workload(
    kernel=obtain_resource("riscv-linux-6.8.12-kernel", resource_version="1.0.0"),
    bootloader=obtain_resource(
        "riscv-bootloader-opensbi-1.3.1", resource_version="1.0.0"
    ),
    disk_image=CustomDiskImageResource(
        "/root/.cache/gem5/riscv-ubuntu-24.04-img-2.0.0",
        root_partition="1",
    ),
    # The after_boot user is unprivileged `gem5` and can't reach the binary in
    # /root. This script (delivered fresh via readfile) carries the binary as
    # base64, decodes it into gem5's $HOME, and runs it there -- no /root, no
    # sudo, no disk edit, so the checkpoint stays valid. Regenerate the script
    # after recompiling the binary; the same checkpoint keeps working.
    readfile="configs/pimony/run_pim_test.sh",
)

# Restore instead of boot: gem5 reads this dir at instantiate and resumes.
board._checkpoint = Path(CHECKPOINT_DIR)

# Default hypercall-3 handler stops the sim when after_boot.sh finishes (i.e.
# when the binary is done) -- no exit-handler overrides needed.
simulator = Simulator(board=board)
simulator.run()
