#!/usr/bin/env python3
"""skip: plumbing. Thesis 6.6: overlap and attention heads on PIM, GPT-3 layer on O3 (gpt3_hs2_* runs).

Sections = the eight stats blocks of the layer: 1 LN1 + QKV, 2 attention, 3 Wo, 4 residual 1 + LN2,
5 W1, 6 GELU, 7 W2, 8 residual 2. cycles = simTicks / 333, asleep = cycles - numCycles.
Usage: python3 experiments/6.6_overlap/parse.py [OUT]   OUT = run dir relative to gem5/, default m5out
Prints the numbers and writes runs.csv, overlap.csv and fig6_5.csv next to this script.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from common import PERIOD, blocks, write_csv  # noqa: E402

SECTIONS = range(1, 9)
L2 = "system.l2cache."


def sections(run):
    """[(cycles, asleep cycles)] for sections 1..8."""
    v = blocks(run, "simTicks", "system.cpu.numCycles")
    return [(v["simTicks"][s] // PERIOD, v["simTicks"][s] // PERIOD - v["system.cpu.numCycles"][s]) for s in SECTIONS]


def l2_miss_latency(run, s=(1, 2), who="total"):
    """Average L2 demand-miss latency over sections s, in ticks; who = total or cpu.data (CPU loads)."""
    v = blocks(run, f"{L2}demandMissLatency::{who}", f"{L2}demandMisses::{who}")
    lat, miss = v[f"{L2}demandMissLatency::{who}"], v[f"{L2}demandMisses::{who}"]
    return sum(lat[b] for b in s) / sum(miss[b] for b in s)


def cpu_l2_misses(run):
    v = blocks(run, f"{L2}demandMisses::cpu.data")[f"{L2}demandMisses::cpu.data"]
    return sum(v[s] for s in SECTIONS)


RUNS = {"n0": "gpt3_hs2_n0_o3", "mm": "gpt3_hs2_mm_o3", "att": "gpt3_hs2_att_o3",
        "n28": "gpt3_hs2_n28_o3", "n32": "gpt3_hs2_n32_o3"}
sec = {k: sections(r) for k, r in RUNS.items()}

# Per run: layer, attention and sleep
rows = []
for k, run in RUNS.items():
    s = sec[k]
    rows.append({"run": run, "layer_cycles": sum(c for c, _ in s), "attention_cycles": s[1][0],
                 "asleep_cycles": sum(a for _, a in s), "attention_asleep": s[1][1],
                 "cpu_l2_misses": cpu_l2_misses(run)})
write_csv(HERE / "runs.csv", rows)
print(f"{'run':16s} {'layer':>10s} {'attention':>10s} {'asleep':>9s} {'attn asleep':>11s} {'CPU L2 misses':>13s}")
for r in rows:
    print(f"{r['run']:16s} {r['layer_cycles']:10,d} {r['attention_cycles']:10,d} {r['asleep_cycles']:9,d}"
          f" {r['attention_asleep']:11,d} {r['cpu_l2_misses']:13,d}")
by = {r["run"]: r for r in rows}
ref = by[RUNS["n0"]]

print("\nReference run (no overlap, all heads on the CPU)")
sleep = ref["asleep_cycles"]
print(f"CPU asleep: {100 * sleep / ref['layer_cycles']:.1f}% of the layer;"
      f" Wo + W1 + W2 hold {100 * sum(sec['n0'][s][1] for s in (2, 4, 6)) / sleep:.1f}% of it,"
      f" QKV {100 * sec['n0'][0][1] / sleep:.1f}%")
print(f"Attention: {100 * ref['attention_cycles'] / ref['layer_cycles']:.1f}% of the layer,"
      f" {ref['attention_cycles'] / sleep:.2f}x the CPU's total sleep (the PIM is idle in it)")

# Overlaps at N=0, sections 1 + 2 together, and the L2 miss latency there
overlap = []
for k, label in (("n0", "no overlap"), ("mm", "1st overlap: Q, K read-back"), ("att", "2nd overlap: scores early")):
    overlap.append({"run": RUNS[k], "mode": label, "qkv_attention_cycles": sec[k][0][0] + sec[k][1][0],
                    "l2_miss_latency_ticks": round(l2_miss_latency(RUNS[k])),
                    "l2_miss_latency_cpu_loads_ticks": round(l2_miss_latency(RUNS[k], who="cpu.data"))})
write_csv(HERE / "overlap.csv", overlap)
print("\nOverlap (sections 1 + 2, N=0)")
for i, r in enumerate(overlap):
    prev = overlap[max(i - 1, 0)]["qkv_attention_cycles"]
    l0, c0 = overlap[0]["l2_miss_latency_ticks"], overlap[0]["l2_miss_latency_cpu_loads_ticks"]
    print(f"{r['mode']:28s} {r['qkv_attention_cycles']:10,d} ({100 * (r['qkv_attention_cycles'] / prev - 1):+.1f}%"
          f" vs the row above)  L2 miss latency {100 * (r['l2_miss_latency_ticks'] / l0 - 1):+.1f}%"
          f" (CPU loads {100 * (r['l2_miss_latency_cpu_loads_ticks'] / c0 - 1):+.1f}%)")

print("\nAttention heads on PIM (vs N=0)")
for k in ("n28", "n32"):
    r = by[RUNS[k]]
    print(f"{k:4s} attention {100 * (r['attention_cycles'] / ref['attention_cycles'] - 1):+.1f}%,"
          f" layer {100 * (r['layer_cycles'] / ref['layer_cycles'] - 1):+.1f}%")
r = by[RUNS["n32"]]
awake = r["attention_cycles"] - r["attention_asleep"]
print(f"All 32 on PIM: CPU asleep {100 * r['attention_asleep'] / r['attention_cycles']:.1f}% of attention;"
      f" CPU L2 misses x{r['cpu_l2_misses'] / ref['cpu_l2_misses']:.2f};"
      f" CPU time per head x{awake / ref['attention_cycles']:.2f} of a CPU head")

# Figure 6.5: per section, all heads on the CPU vs all heads on PIM
fig = [{"bar": bar, "run": RUNS[k], "section": s, "cycles": c, "asleep_cycles": a}
       for bar, k in (("all_heads_on_cpu", "n0"), ("all_heads_on_pim", "n32"))
       for s, (c, a) in zip(SECTIONS, sec[k])]
write_csv(HERE / "fig6_5.csv", fig)
