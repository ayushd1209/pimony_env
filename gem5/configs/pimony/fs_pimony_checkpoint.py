# Make a "fully booted" checkpoint for the PIM experiments.
#
# Boots the SAME binary disk that fs_pimony_run.py uses, under ATOMIC (fast),
# and saves a checkpoint at hypercall 2 -- the moment after_boot.sh STARTS.
# At that point Ubuntu userspace is fully up but the test binary has NOT run
# yet (the binary is launched by after_boot.sh via `m5 readfile`, which fires
# AFTER this hypercall). So the slow boot is paid exactly once, here.
#
# The checkpoint is reusable: it does not contain the binary command. That
# command lives in fs_pimony_restore.py, read via `m5 readfile` after restore.
#
#   build/RISCV/gem5.opt configs/pimony/fs_pimony_checkpoint.py
#   # -> writes checkpoint to ./pim_fullboot_ckpt/
#
# Pair with: fs_pimony_restore.py  (restores this, switches to TIMING, runs ROI)

from gem5.components.boards.riscv_board import RiscvBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_walk_cache_hierarchy import (
    PrivateL1PrivateL2WalkCacheHierarchy,
)
from gem5.components.memory.pimony import PIMony_SingleChannelLPDDR
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import obtain_resource, CustomDiskImageResource
from gem5.simulate.exit_handler import AfterBootExitHandler
from gem5.simulate.simulator import Simulator
from gem5.utils.override import overrides

# Where the checkpoint is written. Kept OUT of m5out so restore can always
# find it regardless of per-run output churn.
CHECKPOINT_DIR = "pim_fullboot_ckpt"

cache_hierarchy = PrivateL1PrivateL2WalkCacheHierarchy(
    l1d_size="16KiB", l1i_size="16KiB", l2_size="256KiB"
)

# Memory/cache/board MUST match the restore script (CPU type may differ).
memory = PIMony_SingleChannelLPDDR(
    "3GiB",
    "configs/pimony/pimony_mem.json",
    "ext/dramsim3/PIMony/configs/model_configs/gpt3-2.7B_single_layer.json",
    "off",
)

# ATOMIC = fast functional boot. We only care about reaching post-boot.
processor = SimpleProcessor(
    cpu_type=CPUTypes.ATOMIC, isa=ISA.RISCV, num_cores=1
)

board = RiscvBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# Same binary disk as fs_pimony_run.py, by PATH -> no checksum/re-download.
# (readfile_contents is harmless here; it is never read -- we checkpoint and
# exit before `m5 readfile` runs. Kept for config parity with the run script.)
board.set_kernel_disk_workload(
    kernel=obtain_resource("riscv-linux-6.8.12-kernel", resource_version="1.0.0"),
    bootloader=obtain_resource(
        "riscv-bootloader-opensbi-1.3.1", resource_version="1.0.0"
    ),
    disk_image=CustomDiskImageResource(
        "/root/.cache/gem5/riscv-ubuntu-24.04-img-2.0.0",
        root_partition="1",
    ),
    readfile_contents="/root/pim_dispatch_wait_test\n",
)


# Hypercall 2 fires when after_boot.sh starts: userspace fully booted, binary
# not yet run. Save the checkpoint here and stop. (NOT hypercall 1, which is
# kernel-only -- that was the earlier mistake.)
class CheckpointAtFullBoot(AfterBootExitHandler):
    @overrides(AfterBootExitHandler)
    def _process(self, simulator: "Simulator") -> None:
        print(f"Userspace booted -- saving checkpoint to {CHECKPOINT_DIR}")
        simulator.save_checkpoint(CHECKPOINT_DIR)
        print("Checkpoint saved. Boot paid once; restore is instant from here.")

    @overrides(AfterBootExitHandler)
    def _exit_simulation(self) -> bool:
        return True  # stop right after saving


simulator = Simulator(board=board)
simulator.run()
