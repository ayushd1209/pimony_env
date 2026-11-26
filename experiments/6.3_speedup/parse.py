#!/usr/bin/env python3
"""skip: plumbing. Thesis 6.3: the numbers of Table 6.2 and Figure 6.2, from the runs' stats.txt.

Usage: python3 experiments/6.3_speedup/parse.py [OUT]   OUT = run dir relative to gem5/, default m5out
Prints both and writes table6_2.csv and fig6_2.csv next to this script.
"""
import csv
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
M5 = REPO / "gem5" / (sys.argv[1] if len(sys.argv) > 1 else "m5out")
PIMONY_LOGS = REPO / "gem5/configs/scratch/progs/pimony_baseline/logs"
PERIOD, TCK = 333, 937        # CPU / DRAM clock period in ps
MATMUL_BLOCKS = (1, 3, 5, 7)  # stats dumps of sections 1, 3, 5, 7: QKV, Wo, W1, W2
HOSTS = ("minor", "o3", "o3v61")
RUNS = {  # workload: (CPU baseline run prefix, PIM run prefix)
    "BERT": ("bert_blas_fair_scrub", "bert_pim_g7pt_scrub64"),
    "GPT-3": ("gpt3_blas_scrub", "gpt3_scr64"),
}


def matmul(run):
    """(cycles, CPU-asleep cycles) summed over the matmul blocks; asleep = cycles - numCycles."""
    ticks, awake, block = {}, {}, 0
    with open(M5 / run / "stats.txt") as f:
        for line in f:
            if "Begin Simulation Statistics" in line:
                block += 1
            elif line.startswith("simTicks "):
                ticks[block] = int(line.split()[1])
            elif line.startswith("system.cpu.numCycles "):
                awake[block] = int(line.split()[1])
    cycles = sum(ticks[b] // PERIOD for b in MATMUL_BLOCKS)
    return cycles, cycles - sum(awake[b] for b in MATMUL_BLOCKS)


def pimony_alone(sub, prefix):
    """PIMony without gem5: 'GEMV done latency' over the four matmul logs, DRAM -> CPU cycles."""
    dram = sum(int(line.split()[-2]) for m in ("qkv", "wo", "w1", "w2")
               for line in open(PIMONY_LOGS / sub / f"{prefix}_{m}.log") if "GEMV done latency" in line)
    return round(dram * TCK / PERIOD)


def write(name, rows):
    with open(HERE / name, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader()
        w.writerows(rows)


# Table 6.2: matmul speedup over OpenBLAS
table = []
for workload, (base, pim) in RUNS.items():
    for host in HOSTS:
        b, p = matmul(f"{base}_{host}")[0], matmul(f"{pim}_{host}")[0]
        table.append({"workload": workload, "host": host, "baseline_run": f"{base}_{host}",
                      "pim_run": f"{pim}_{host}", "baseline_cycles": b, "pim_cycles": p,
                      "speedup": round(b / p, 2)})
write("table6_2.csv", table)
print("Table 6.2")
print(f"{'workload':8s} {'host':6s} {'baseline':>12s} {'PIM':>10s} {'speedup':>8s}")
for r in table:
    print(f"{r['workload']:8s} {r['host']:6s} {r['baseline_cycles']:12,d} {r['pim_cycles']:10,d}"
          f" {r['speedup']:7.2f}x")

# Figure 6.2: GPT-3 matmul time, PIMony alone and the PIM run on each host
fig = [{"bar": "pimony_alone", "source": "pimony_baseline/logs/standalone/gpt3_*",
        "cycles": pimony_alone("standalone", "gpt3"), "asleep_cycles": ""}]
for host in HOSTS:
    cycles, asleep = matmul(f"gpt3_scr64_{host}")
    fig.append({"bar": host, "source": f"gpt3_scr64_{host}", "cycles": cycles, "asleep_cycles": asleep})
fig.append({"bar": "pimony_same_jobs", "source": "pimony_baseline/logs/aligned/seq_*",
            "cycles": pimony_alone("aligned", "seq"), "asleep_cycles": ""})
write("fig6_2.csv", fig)
print("\nFigure 6.2 (GPT-3)")
for r in fig:
    awake = f"  asleep {r['asleep_cycles']:>9,}  awake {r['cycles'] - r['asleep_cycles']:>9,}" \
        if r["asleep_cycles"] != "" else ""
    print(f"{r['bar']:17s} cycles {r['cycles']:>9,}{awake}")

# VLEN 256 vs 512: BERT matmul on O3, same binaries
vlen = []
for v in (256, 512):
    b = matmul(f"vlen/bert_blas_fair_scrub_o3_v{v}")[0]
    p = matmul(f"vlen/bert_pim_g7pt_scrub64_o3_v{v}")[0]
    vlen.append({"vlen": v, "baseline_cycles": b, "pim_cycles": p, "speedup": round(b / p, 2)})
write("vlen.csv", vlen)
print("\nVLEN 256 vs 512 (BERT, O3)")
for r in vlen:
    print(f"VLEN {r['vlen']}  baseline {r['baseline_cycles']:>9,}  PIM {r['pim_cycles']:>7,}  {r['speedup']:.2f}x")
b0, b1, p0, p1 = vlen[0]["baseline_cycles"], vlen[1]["baseline_cycles"], vlen[0]["pim_cycles"], vlen[1]["pim_cycles"]
print(f"512 vs 256: baseline {100 * (b1 / b0 - 1):+.1f}%, PIM {100 * (p1 / p0 - 1):+.1f}%")
