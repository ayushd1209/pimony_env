#!/usr/bin/env python3
"""skip: plumbing. Thesis 6.3 / Table 6.2: matmul speedup over OpenBLAS, from the 12 runs' stats.txt.

Usage: python3 experiments/6.3_speedup/parse.py [OUT]   OUT = run dir relative to gem5/, default m5out
Prints the table and writes results.csv next to this script.
"""
import csv
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
M5 = HERE.parents[1] / "gem5" / (sys.argv[1] if len(sys.argv) > 1 else "m5out")
TICKS_PER_CYCLE = 333        # gem5 CPU clock period in ps
MATMUL_BLOCKS = (1, 3, 5, 7)  # stats dumps of sections 1, 3, 5, 7: QKV, Wo, W1, W2
HOSTS = ("minor", "o3", "o3v61")
RUNS = {  # workload: (CPU baseline run prefix, PIM run prefix)
    "BERT": ("bert_blas_fair_scrub", "bert_pim_g7pt_scrub64"),
    "GPT-3": ("gpt3_blas_scrub", "gpt3_scr64"),
}


def matmul_cycles(run):
    ticks, block = {}, 0
    with open(M5 / run / "stats.txt") as f:
        for line in f:
            if "Begin Simulation Statistics" in line:
                block += 1
            elif line.startswith("simTicks "):
                ticks[block] = int(line.split()[1])
    return sum(ticks[b] for b in MATMUL_BLOCKS) // TICKS_PER_CYCLE


rows = []
for workload, (base, pim) in RUNS.items():
    for host in HOSTS:
        b, p = matmul_cycles(f"{base}_{host}"), matmul_cycles(f"{pim}_{host}")
        rows.append({"workload": workload, "host": host, "baseline_run": f"{base}_{host}",
                     "pim_run": f"{pim}_{host}", "baseline_cycles": b, "pim_cycles": p,
                     "speedup": round(b / p, 2)})

print(f"{'workload':8s} {'host':6s} {'baseline':>12s} {'PIM':>10s} {'speedup':>8s}")
for r in rows:
    print(f"{r['workload']:8s} {r['host']:6s} {r['baseline_cycles']:12,d} {r['pim_cycles']:10,d}"
          f" {r['speedup']:7.2f}x")
with open(HERE / "results.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=rows[0].keys())
    w.writeheader()
    w.writerows(rows)
