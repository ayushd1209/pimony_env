#!/usr/bin/env python3
"""skip: plumbing. Thesis 6.4: pim.gemv vs pim.dispatch on O3, from the runs' stats.txt and run.log.

Usage: python3 experiments/6.4_granularity/parse.py [OUT]   OUT = run dir relative to gem5/, default m5out
Prints the numbers and writes table6_3.csv, fig6_3.csv and details.csv next to this script.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from common import M5, REPO, matmul, pimony_alone, total, write_csv  # noqa: E402

WORKLOADS = {  # workload: (pim.gemv run, 32 commands then wait, 1 command then wait)
    "BERT": ("bert_pim_g7pt_scrub64_o3", "bert_disp_w32_scrub64_o3", "bert_disp_scrub64_o3"),
    "GPT-3": ("gpt3_scr64_o3", "gpt3_disp_w32_scr64_o3", "gpt3_disp_w1_scr64_o3"),
}
SAME_JOBS = {"BERT": "seq_bert_g7pt", "GPT-3": "seq"}  # PIMony alone running pim.gemv's jobs
MAC_SENT = re.compile(r"AddTransaction\(PIM\).*\| MAC \|")
ENGINE_GAPS = REPO / "gem5/configs/scratch/progs/engine_gaps.py"


def macs_sent(run):
    """MAC commands PIMony received; needs the MAC lines kept in run.log."""
    return sum(1 for line in open(M5 / run / "run.log", errors="replace") if MAC_SENT.search(line))


def smallest_gap(run):
    """DRAM cycles from an engine's previous MAC to the next one reaching it (engine_gaps.py)."""
    out = subprocess.run([sys.executable, ENGINE_GAPS, M5 / run / "run.log"], capture_output=True, text=True).stdout
    return int(re.search(r"smallest gap.*: (\d+) DRAM clk", out).group(1))


# Table 6.3: matmul cycles for each way of issuing the same MAC commands
table = []
for workload, (gemv, w32, w1) in WORKLOADS.items():
    g, a, c = matmul(gemv)[0], matmul(w32)[0], matmul(w1)[0]
    table.append({"workload": workload, "pim_gemv": g, "dispatch_32_then_wait": a, "factor_32": round(a / g, 2),
                  "dispatch_1_then_wait": c, "factor_1": round(c / g, 2)})
write_csv(HERE / "table6_3.csv", table)
print("Table 6.3 (O3)")
print(f"{'workload':8s} {'pim.gemv':>10s} {'32 then wait':>18s} {'1 then wait':>20s}")
for r in table:
    print(f"{r['workload']:8s} {r['pim_gemv']:10,d} {r['dispatch_32_then_wait']:10,d} ({r['factor_32']:.2f}x)"
          f" {r['dispatch_1_then_wait']:11,d} ({r['factor_1']:.2f}x)")

# Figure 6.3: GPT-3, CPU asleep and awake for pim.gemv and 32-then-wait pim.dispatch
fig = []
for bar, run in (("pim.gemv", "gpt3_scr64_o3"), ("pim.dispatch", "gpt3_disp_w32_scr64_o3")):
    cycles, asleep = matmul(run)
    fig.append({"bar": bar, "source": run, "cycles": cycles, "asleep_cycles": asleep})
fig.append({"bar": "pimony_same_jobs", "source": "pimony_baseline/logs/aligned/seq_*",
            "cycles": pimony_alone("aligned", "seq"), "asleep_cycles": ""})
write_csv(HERE / "fig6_3.csv", fig)
print("\nFigure 6.3 (GPT-3)")
for r in fig:
    awake = f"  asleep {r['asleep_cycles']:>10,}  awake {r['cycles'] - r['asleep_cycles']:>10,}" \
        if r["asleep_cycles"] != "" else ""
    print(f"{r['bar']:17s} cycles {r['cycles']:>10,}{awake}")

# Numbers in the text
details = []
for workload, (gemv, w32, w1) in WORKLOADS.items():
    for run in (gemv, w32, w1):
        details.append({"what": "dispatch run" if run != gemv else "pim.gemv run", "run": run,
                        "waits": total(run, "system.workload.inst.quiesce"),
                        "macs_sent": macs_sent(run) if run != gemv else "",
                        "value": smallest_gap(run) if run == w32 else ""})
    asleep, same = matmul(gemv)[1], pimony_alone("aligned", SAME_JOBS[workload])
    details.append({"what": "pim.wait asleep vs PIMony same jobs (%)", "run": gemv, "waits": "",
                    "macs_sent": "", "value": round(100 * (asleep / same - 1), 1)})
cycles, asleep = matmul("gpt3_disp_w1_scr64_o3")
details.append({"what": "1 then wait, GPT-3: asleep / awake cycles", "run": "gpt3_disp_w1_scr64_o3",
                "waits": "", "macs_sent": "", "value": f"{asleep}/{cycles - asleep}"})
details.append({"what": "192 at once: MACs PIMony received before aborting", "run": "bert_disp_w192_o3_f2",
                "waits": "", "macs_sent": macs_sent("bert_disp_w192_o3_f2"), "value": ""})
write_csv(HERE / "details.csv", details)
print("\nIn the text")
for r in details:
    if r["what"] == "pim.gemv run":
        print(f"{r['run']:26s} waits {r['waits']:>6,}")
    elif r["what"] == "dispatch run":
        gap = f"  smallest engine gap {r['value']} DRAM cycles" if r["value"] != "" else ""
        print(f"{r['run']:26s} waits {r['waits']:>6,}  MACs sent {r['macs_sent']:>6,}{gap}")
    else:
        print(f"{r['what']}: {r['value']}{r['macs_sent']}  ({r['run']})")
