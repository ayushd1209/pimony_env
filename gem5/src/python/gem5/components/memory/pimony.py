import m5
import os
import configparser

from m5.objects import DRAMsim3, AddrRange, Port, MemCtrl
from m5.util.convert import toMemorySize

from ...utils.override import overrides
from ..boards.abstract_board import AbstractBoard
from .abstract_memory_system import AbstractMemorySystem


from typing import Optional, Tuple, Sequence, List


def config_ds3(model_name: str) -> Tuple[str, str]:
    """
    This function creates a config file that will be used to create a memory
    controller of type DRAMSim3. It stores the config file in /tmp/ directory.

    :param num_chnls: The number of channels to configure for the memory
    :returns: A tuple containing the output file and the output directory.
    """
    mem_config = "/home/spec-2017/gem5/ext/dramsim3/PIMony/configs/memory_configs/pimony.json"
    model_config = "/home/spec-2017/gem5/ext/dramsim3/PIMony/configs/model_configs"
    model_config = os.path.join(model_config, model_name + ".json")
    # log_dir = "/home/spec-2017/gem5/ext/dramsim3/PIMony/experiment_logs"
    log_level = "off"


    if not os.path.isfile(mem_config):
        raise Exception(
            "The configuration file '" + mem_config + "' cannot  be found."
        )
    if not os.path.isfile(model_config):
        raise Exception(
            "The configuration file '" + model_config + "' cannot  be found."
        )
    return mem_config, model_config, m5.options.outdir, log_level


class DRAMSim3MemCtrl(DRAMsim3):
    """
    A DRAMSim3 Memory Controller.

    The class serves as a SimObject object wrapper, utiliszing the DRAMSim3
    configuratons.
    """

    def __init__(self, mem_config: str, model_config: str, log_level: str) -> None:
        """
        :param mem_name: The name of the type  of memory to be configured.
        :param num_chnls: The number of channels.
        """
        super().__init__()
        self.mem_config = mem_config
        self.model_config = model_config
        self.log_dir = m5.options.outdir
        self.log_level = log_level


class SingleChannel(AbstractMemorySystem):
    """
    A Single Channel Memory system.
    """

    def __init__(self, mem_config: str, model_config: str, log_level: str, size: Optional[str]):
        super().__init__()

        self.mem_ctrl = DRAMSim3MemCtrl(mem_config, model_config, log_level)
        self._size = toMemorySize(size)
        if not size:
            raise NotImplementedError(
                "DRAMSim3 memory controller requires a size parameter."
            )

    @overrides(AbstractMemorySystem)
    def incorporate_memory(self, board: AbstractBoard) -> None:
        pass

    @overrides(AbstractMemorySystem)
    def get_mem_ports(self) -> Tuple[Sequence[AddrRange], Port]:
        return [(self.mem_ctrl.range, self.mem_ctrl.port)]

    @overrides(AbstractMemorySystem)
    def get_memory_controllers(self) -> List[MemCtrl]:
        return [self.mem_ctrl]

    @overrides(AbstractMemorySystem)
    def get_size(self) -> int:
        return self._size

    @overrides(AbstractMemorySystem)
    def set_memory_range(self, ranges: List[AddrRange]) -> None:
        if len(ranges) != 1 or ranges[0].size() != self._size:
            raise Exception(
                "Single channel DRAMSim memory controller requires a single "
                "range which matches the memory's size."
            )
        self.mem_ctrl.range = ranges[0]


def PIMony_SingleChannelLPDDR(size: Optional[str] = "1024MB",
                                    mem_config : str = "",
                                    model_config : str = "",
                                    log_level : str = "") -> SingleChannel:
    """
    :param size: The size of the memory system. Default value of 1024MB.
    """
    return SingleChannel(mem_config, model_config, log_level, size)

