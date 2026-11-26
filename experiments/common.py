"""skip: plumbing. Shared by the experiments' parse.py: read gem5 stats.txt and PIMony logs, write CSVs.

Run dirs are relative to gem5/: the first command-line argument of parse.py, default m5out.
"""
import csv
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
M5 = REPO / "gem5" / (sys.argv[1] if len(sys.argv) > 1 else "m5out")
PIMONY_LOGS = REPO / "gem5/configs/scratch/progs/pimony_baseline/logs"
PERIOD, TCK = 333, 937        # CPU / DRAM clock period in ps
MATMUL_BLOCKS = (1, 3, 5, 7)  # stats dumps of sections 1, 3, 5, 7: QKV, Wo, W1, W2


def blocks(run, *names):
    """{stat name: {stats block number: value}} for the given stat names; blocks count from 1."""
    vals, block = {n: {} for n in names}, 0
    with open(M5 / run / "stats.txt") as f:
        for line in f:
            if "Begin Simulation Statistics" in line:
                block += 1
                continue
            parts = line.split()
            if parts and parts[0] in vals:
                vals[parts[0]][block] = float(parts[1]) if "." in parts[1] else int(parts[1])
    return vals


def matmul(run):
    """(cycles, CPU-asleep cycles) summed over the matmul blocks; asleep = cycles - numCycles."""
    v = blocks(run, "simTicks", "system.cpu.numCycles")
    cycles = sum(v["simTicks"][b] // PERIOD for b in MATMUL_BLOCKS)
    return cycles, cycles - sum(v["system.cpu.numCycles"][b] for b in MATMUL_BLOCKS)


def total(run, name):
    """A stat summed over every stats block of the run."""
    return sum(blocks(run, name)[name].values())


def pimony_alone(sub, prefix):
    """PIMony without gem5: 'GEMV done latency' over the four matmul logs, DRAM -> CPU cycles."""
    dram = sum(int(line.split()[-2]) for m in ("qkv", "wo", "w1", "w2")
               for line in open(PIMONY_LOGS / sub / f"{prefix}_{m}.log") if "GEMV done latency" in line)
    return round(dram * TCK / PERIOD)


def write_csv(path, rows):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader()
        w.writerows(rows)
