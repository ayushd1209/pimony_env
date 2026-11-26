#!/usr/bin/env python3
"""skip: plumbing. Thesis 6.5: cost of pim.fence (Figure 6.4 and text) and Table 6.4.

Fence cost = matmul cycles of the PIM run minus those of its fence-free control. Fences are
counted from the L1D cache-maintenance requests (CleanSharedReq + InvalidateReq) in the matmul blocks.
Usage: python3 experiments/6.5_fence/parse.py [OUT]   OUT = run dir relative to gem5/, default m5out
Prints the numbers and writes fence_cost.csv, fig6_4.csv and table6_4.csv next to this script.
"""
import csv
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from common import PERIOD, REPO, matmul, matmul_sum, total, write_csv  # noqa: E402

D = "system.cpu.dcache."
CLEANS = (D + "CleanSharedReq.mshrMisses::total", D + "CleanSharedReq.mshrHits::total")
INVALIDATES = (D + "InvalidateReq.mshrMisses::total", D + "InvalidateReq.mshrHits::total")
BENCHMARK = REPO / "gem5/tests/test-progs/fence_compare/validated/measurements.tsv"
PAIRS = [  # workload, CPU, control kind, run with fences, control
    ("GPT-3", "o3", "_nf", "gpt3_scr64_o3", "gpt3_scr64_nf_o3"),
    ("GPT-3", "o3", "_nfd", "gpt3_scr64_o3", "gpt3_scr64_nfd_o3"),
    ("GPT-3", "minor", "_nfd", "gpt3_scr64_minor", "gpt3_scr64_nfd_minor"),
    ("BERT", "o3", "_nf", "bert_pim_g7pt_scrub64_o3", "bert_pim_g7pt_scrub64_nf_o3"),
    ("BERT", "minor", "_nfd", "bert_pim_g7pt_scrub64_minor", "bert_pim_g7pt_scrub64_nfd_minor"),
    ("BERT unscrubbed", "minor", "_nfd", "bert_pim_g7pt_minor", "bert_pim_g7pt_nfd_minor"),
]


def fences(run):
    return matmul_sum(run, *CLEANS, *INVALIDATES)


# Fence cost per workload and CPU
cost = []
for workload, cpu, kind, on, off in PAIRS:
    c_on, c_off = matmul(on)[0], matmul(off)[0]
    removed = fences(on) - fences(off)
    cost.append({"workload": workload, "cpu": cpu, "control": kind, "run": on, "control_run": off,
                 "cycles": c_on, "control_cycles": c_off, "fence_cycles": c_on - c_off,
                 "fences_removed": removed, "per_fence": round((c_on - c_off) / removed, 1),
                 "share_percent": round(100 * (c_on - c_off) / c_on, 1)})
write_csv(HERE / "fence_cost.csv", cost)
print("Fence cost (matmul blocks)")
print(f"{'workload':16s} {'cpu':5s} {'ctrl':4s} {'fences on':>10s} {'off':>10s} {'cost':>8s} {'removed':>7s}"
      f" {'/fence':>6s} {'share':>6s}")
for r in cost:
    print(f"{r['workload']:16s} {r['cpu']:5s} {r['control']:4s} {r['cycles']:10,d} {r['control_cycles']:10,d}"
          f" {r['fence_cycles']:8,d} {r['fences_removed']:7,d} {r['per_fence']:6.1f} {r['share_percent']:5.1f}%")

# Fences per layer and the latency of the cache operation under one fence
print("\nFences in the matmul blocks, and the cache operation under a fence")
for run in ("gpt3_scr64_o3", "gpt3_scr64_minor", "bert_pim_g7pt_scrub64_o3", "bert_pim_g7pt_scrub64_minor"):
    cl, inv, desc = matmul_sum(run, *CLEANS), matmul_sum(run, *INVALIDATES), total(run, "system.workload.inst.quiesce")
    lat = matmul_sum(run, D + "CleanSharedReq.mshrMissLatency::total", D + "InvalidateReq.mshrMissLatency::total")
    miss = matmul_sum(run, D + "CleanSharedReq.mshrMisses::total", D + "InvalidateReq.mshrMisses::total")
    print(f"{run:28s} {cl + inv:5,d} = {cl - desc:5,d} input cleans + {desc:2d} descriptor cleans"
          f" + {inv:5,d} result invalidates   cache op {lat / miss / PERIOD:.1f} cycles")

# Figure 6.4: GPT-3, asleep / other awake work / fences, per CPU
fig = []
for cpu, on, off in (("minor", "gpt3_scr64_minor", "gpt3_scr64_nfd_minor"), ("o3", "gpt3_scr64_o3", "gpt3_scr64_nf_o3")):
    (cycles, asleep), control = matmul(on), matmul(off)[0]
    fig.append({"bar": cpu, "run": on, "control": off, "cycles": cycles, "asleep_cycles": asleep,
                "control_cycles": control, "fence_cycles": cycles - control})
write_csv(HERE / "fig6_4.csv", fig)
print("\nFigure 6.4 (GPT-3)")
for r in fig:
    other = r["cycles"] - r["asleep_cycles"] - r["fence_cycles"]
    print(f"{r['bar']:5s} cycles {r['cycles']:9,d}  asleep {r['asleep_cycles']:7,d}  other awake {other:9,d}"
          f"  fences {r['fence_cycles']:7,d} ({100 * r['fence_cycles'] / r['cycles']:.1f}%)")

# Table 6.4: the separate cache-cleaning benchmark (gem5/tests/test-progs/fence_compare)
table = {}
for r in csv.DictReader(open(BENCHMARK), delimiter="\t"):
    table.setdefault((r["cpu"], int(r["lines"])), {})[r["approach"]] = int(r["elapsed_cycles"])
rows = [{"cpu": cpu, "lines": lines, "pim_fence_cl": v["pim.fence.cl/line"],
         "standard_per_line": v["cbo.clean+FENCE/line"], "standard_batched": v["batched cbo.clean+FENCE"]}
        for (cpu, lines), v in table.items()]
write_csv(HERE / "table6_4.csv", rows)
print("\nTable 6.4 (cycles to clean N dirty lines)")
print(f"{'cpu':6s} {'lines':>5s} {'pim.fence.cl':>13s} {'per-line':>9s} {'batched':>8s}")
for r in rows:
    print(f"{r['cpu']:6s} {r['lines']:5d} {r['pim_fence_cl']:13,d} {r['standard_per_line']:9,d} {r['standard_batched']:8,d}")
