#!/usr/bin/env python3
"""skip: plumbing. Thesis Figure 6.5 from fig6_5.csv (written by parse.py), thesis figure style.

One GPT-3 decoder layer to scale on O3, all heads on the CPU vs all heads on PIM. CPU lane: awake
(charcoal) and asleep in pim.wait (solid blue), summed per section, awake first. PIM lane: busy
(blue stripes) while the CPU sleeps, idle (white) elsewhere. Reproduces the thesis figure byte-for-byte (SVG).
Usage: python3 experiments/6.6_overlap/plot.py [--svg-only]  ->  fig6_5.{svg,pdf,png} next to this script
"""
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../../docs/diagrams"))
from figstyle import *  # noqa: E402,F403

# ---------------------------------------------------------------- data
SECTIONS = ["LN 1 + QKV", "attention", "W_o", "residual 1 + LN 2", "W_1", "GELU", "W_2", "residual 2"]
TITLES = {"all_heads_on_cpu": "All heads on CPU", "all_heads_on_pim": "All heads on PIM"}
rows = list(csv.DictReader(open(os.path.join(HERE, "fig6_5.csv"))))
# (title, run, per section (total cycles, asleep cycles))
RUNS = [(TITLES[b], next(r["run"] for r in rows if r["bar"] == b),
         [(int(r["cycles"]), int(r["asleep_cycles"])) for r in sorted((r for r in rows if r["bar"] == b),
                                                                      key=lambda r: int(r["section"]))])
        for b in ("all_heads_on_cpu", "all_heads_on_pim")]

# ---------------------------------------------------------------- layout
X0, PX = 62, 830 / 3.6e6               # time 0, px per cycle
LANE_H, LANE_GAP = 26, 5
T1, T2 = 14, 40                        # section labels above the CPU lane: tier 1 (fits) / tier 2 (sliver, leader)
ROW_TOP = [0, 195]
LBL = 18

for (title, _, rows), top in zip(RUNS, ROW_TOP):
    ctext(X0, top + 10, title, 19, "sans", 600, anchor="start")
    cpu_y = top + 36 + 54
    pim_y = cpu_y + LANE_H + LANE_GAP
    ctext(X0 - 9, cpu_y + LANE_H / 2, "CPU", LBL, "sans", 500, anchor="end")
    ctext(X0 - 9, pim_y + LANE_H / 2, "PIM", LBL, "sans", 500, anchor="end")
    c, seps = 0, []
    for name, (tot, sl) in zip(SECTIONS, rows):
        x, w = X0 + c * PX, tot * PX
        aw, sw = (tot - sl) * PX, sl * PX
        seg(x, cpu_y, aw, LANE_H, "awake", 0)
        seg(x, pim_y, aw, LANE_H, "idle", 0)
        if sl:
            seg(x + aw, cpu_y, sw, LANE_H, "sleep", 0)
            seg(x + aw, pim_y, sw, LANE_H, "busy", 0)
        seps.append(x)
        if name == "attention" and aw > 200:
            ctext(x + aw / 2, pim_y + LANE_H / 2, "idle", LBL, "sans", 400, SUB)
        line(x, cpu_y - 5, x, cpu_y, 1.0)                                    # section boundary
        cx, lw = x + w / 2, rich_w(name, LBL)
        if lw <= w - 4:
            rich(cx, cpu_y - T1, name, LBL, box=(x, x + w))
        else:
            rich(cx, cpu_y - T2, name, LBL)
            line(cx, cpu_y - T2 + 10, cx, cpu_y, 0.8)
        c += tot
    line(X0 + c * PX, cpu_y - 5, X0 + c * PX, cpu_y, 1.0)
    for sx in seps[1:]:                                                    # section boundaries through both lanes
        line(sx, cpu_y, sx, pim_y + LANE_H, 0.8)
    for y in (cpu_y, pim_y):                                               # lane outlines
        rect(X0, y, c * PX, LANE_H, "none", SW)

# ---- time axis
AY = ROW_TOP[1] + 36 + 54 + 2 * LANE_H + LANE_GAP + 14
line(X0, AY, X0 + 3.6e6 * PX, AY, SW_BOX)
for k in range(8):
    x = X0 + k * 0.5e6 * PX
    line(x, AY, x, AY + 5, SW)
    ctext(x, AY + 19, "0" if k == 0 else f"{k / 2:.1f}", 17)
ctext(X0 + 1.8e6 * PX, AY + 50, "time (million CPU cycles)", LBL, "sans", 500)

# ---- legend
LY = AY + 74
items = [("awake", "CPU awake"), ("sleep", "CPU asleep in `pim.wait`"), ("busy", "PIM busy"), ("idle", "PIM idle")]
total = sum(40 + rich_w(n, LBL) for _, n in items) + 30 * (len(items) - 1)
lx = X0 + 1.8e6 * PX - total / 2
for kind, name in items:
    lx += key(lx, LY, kind, name, LBL) + 30

save(os.path.join(HERE, "fig6_5"),
     4, -4, 940, LY + 20 + 14, "One decoder layer, CPU vs PIM heads")
