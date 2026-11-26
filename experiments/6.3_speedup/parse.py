#!/usr/bin/env python3
"""skip: plumbing. Thesis 6.3: Table 6.2, Figure 6.2 and VLEN 256 vs 512, from the runs' stats.txt.

Usage: python3 experiments/6.3_speedup/parse.py [OUT]   OUT = run dir relative to gem5/, default m5out
Prints the numbers and writes table6_2.csv, fig6_2.csv and vlen.csv next to this script.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from common import matmul, pimony_alone, write_csv  # noqa: E402

HOSTS = ("minor", "o3", "o3v61")
RUNS = {  # workload: (CPU baseline run prefix, PIM run prefix)
    "BERT": ("bert_blas_fair_scrub", "bert_pim_g7pt_scrub64"),
    "GPT-3": ("gpt3_blas_scrub", "gpt3_scr64"),
}

# Table 6.2: matmul speedup over OpenBLAS
table = []
for workload, (base, pim) in RUNS.items():
    for host in HOSTS:
        b, p = matmul(f"{base}_{host}")[0], matmul(f"{pim}_{host}")[0]
        table.append({"workload": workload, "host": host, "baseline_run": f"{base}_{host}",
                      "pim_run": f"{pim}_{host}", "baseline_cycles": b, "pim_cycles": p,
                      "speedup": round(b / p, 2)})
write_csv(HERE / "table6_2.csv", table)
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
write_csv(HERE / "fig6_2.csv", fig)
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
write_csv(HERE / "vlen.csv", vlen)
print("\nVLEN 256 vs 512 (BERT, O3)")
for r in vlen:
    print(f"VLEN {r['vlen']}  baseline {r['baseline_cycles']:>9,}  PIM {r['pim_cycles']:>7,}  {r['speedup']:.2f}x")
b0, b1, p0, p1 = vlen[0]["baseline_cycles"], vlen[1]["baseline_cycles"], vlen[0]["pim_cycles"], vlen[1]["pim_cycles"]
print(f"512 vs 256: baseline {100 * (b1 / b0 - 1):+.1f}%, PIM {100 * (p1 / p0 - 1):+.1f}%")
