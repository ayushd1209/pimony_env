from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject


# TODO: Declare SimpleMemobj as a SimObject.
# You need to expose three ports so Python configs can wire them up:
#   - inst_port  : ResponsePort (CPU sends instruction fetches here)
#   - data_port  : ResponsePort (CPU sends data accesses here)
#   - mem_side   : RequestPort  (this object sends requests toward memory)
#
# The port names here must match the strings you check in getPort() in the .cc.
class SimpleMemobj(SimObject):
    type = "SimpleMemobj"
    cxx_header = "learning_gem5/part2/simple_memobj.hh"
    cxx_class = "gem5::SimpleMemobj"

    inst_port = ResponsePort("CPU side port, receives requests")
    data_port = ResponsePort("CPU side port, receives requests")
    mem_side = RequestPort("Memory side port, sends requests")
