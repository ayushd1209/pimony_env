"""Reuse untouched fs_bert.py, changing only log paths and the simulation cap."""
import os
import runpy
import sys

import m5
from m5.objects import DRAMsim3

config = sys.argv.pop(1)
log_dir = os.path.join(m5.options.outdir, "pimony")
os.makedirs(log_dir, exist_ok=True)
DRAMsim3.log_dir = log_dir
_simulate = m5.simulate


def bounded_simulate(*args, **kwargs):
    return _simulate(*(args or (100_000_000,)), **kwargs)


m5.simulate = bounded_simulate
sys.argv[0] = config
runpy.run_path(config, run_name="__m5_main__")
