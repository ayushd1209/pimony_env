#!/usr/bin/env python3
"""skip: plumbing. Thesis Figure 6.2 from fig6_2.csv (written by parse.py), thesis figure style.

GPT-3 matmul time (blocks 1, 3, 5, 7): PIMony alone, then pim.gemv on three hosts.
Solid blue = CPU asleep in pim.wait, blue stripes = PIM busy with no CPU, charcoal = CPU awake;
dashed = PIMony running our jobs. Reproduces the thesis figure byte-for-byte (SVG).
Usage: python3 experiments/6.3_speedup/plot.py [--svg-only]  ->  fig6_2.{svg,pdf,png} next to this script
"""
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../../docs/diagrams"))
from figstyle import *  # noqa: E402,F403

# ---------------------------------------------------------------- data
LABELS = {"minor": ("MinorCPU", ""), "o3": ("O3CPU", "(4 SIMD units)"), "o3v61": ("O3CPU", "(1 SIMD unit)")}
rows = {r["bar"]: r for r in csv.DictReader(open(os.path.join(HERE, "fig6_2.csv")))}
# host bars: (label, sub-label, run, total cycles, asleep cycles)
HOSTS = [(*LABELS[h], rows[h]["source"], int(rows[h]["cycles"]), int(rows[h]["asleep_cycles"]))
         for h in ("minor", "o3", "o3v61")]
PIMONY_ALONE = int(rows["pimony_alone"]["cycles"])
PIMONY_OURS = int(rows["pimony_same_jobs"]["cycles"])

# ---------------------------------------------------------------- layout
LBL = 18
X0, TOP, PH = 96, 40, 340              # y axis, top of plot (2.5 M), plot height
YMAX = 2.5e6
PXM = PH / YMAX                        # px per cycle
BASE = TOP + PH
BW, PAD, GAP, SEP = 80, 34, 56, 40     # bar width, plot edge to bar, between hosts, bar to separator
XA = X0 + PAD                          # PIMony alone
XS = XA + BW + SEP                     # separator
XH = [XS + SEP + k * (BW + GAP) for k in range(3)]
XR = XH[-1] + BW + PAD                 # right end of the plot

def y_of(c):
    return BASE - c * PXM

# ---- grid and axis
for k in range(6):
    y = y_of(k * 0.5e6)
    if k:
        out.append(f'<path d="M{X0} {y:.2f}H{XR}" stroke="{GRID}" stroke-width="1"/>')
    line(X0 - 5, y, X0, y, SW)
    ctext(X0 - 9, y, "0" if k == 0 else f"{k / 2:.1f}", LBL, anchor="end")
ctext(30, (TOP + BASE) / 2, "CPU cycles (millions)", LBL, "sans", 500, rot=-90)

# ---- bars
def total_label(x, c):
    ctext(x + BW / 2, y_of(c) - 15, f"{c / 1e6:.2f}", LBL, "sans", 500)

seg(XA, y_of(PIMONY_ALONE), BW, PIMONY_ALONE * PXM, "alone", 0.7)
total_label(XA, PIMONY_ALONE)
ctext(XA + BW / 2, BASE + 20, "PIMony alone", LBL)
for (name, sub, _, tot, sl), x in zip(HOSTS, XH):
    seg(x, y_of(sl), BW, sl * PXM, "sleep")
    seg(x, y_of(tot), BW, (tot - sl) * PXM, "awake")
    ctext(x + BW / 2, (y_of(tot) + y_of(sl)) / 2, f"{(tot - sl) / 1e6:.2f}", LBL, "sans", 500, WHITE,
          box=(x, x + BW))
    total_label(x, tot)
    ctext(x + BW / 2, BASE + 20, name, LBL)
    if sub:
        ctext(x + BW / 2, BASE + 41, sub, 15.5, "sans", 400, SUB)
out.append(f'<path d="M{XS} {TOP - 6}V{BASE + 30}" stroke="{LIFE}" stroke-width="1"/>')   # alone | hosts
line(X0, TOP - 6, X0, BASE, SW_BOX)
line(X0, BASE, XR, BASE, SW_BOX)
yr = y_of(PIMONY_OURS)
out.append(f'<path d="M{X0} {yr:.2f}H{XR}" stroke="{INK}" stroke-width="1.6" stroke-dasharray="6 4"/>')

# ---- legend, 2 x 2 below the plot
LY = BASE + 66
items = [("awake", "CPU awake"), ("sleep", "CPU asleep in `pim.wait` (PIM busy)"),
         ("alone", "PIM busy, no CPU"), ("dash", "PIMony alone, same jobs")]
cols = [max(40 + rich_w(n, LBL) for _, n in items[c::2]) for c in (0, 1)]
lx0 = (X0 - 40 + XR) / 2 - (cols[0] + 36 + cols[1]) / 2
for i, (kind, name) in enumerate(items):
    key(lx0 + (i % 2) * (cols[0] + 36), LY + (i // 2) * 32, kind, name, LBL)

save(os.path.join(HERE, "fig6_2"),
     0, 0, round(max(XR + 12, lx0 + cols[0] + 36 + cols[1] + 12)), LY + 32 + 20 + 12,
     "Matmul time, PIMony alone vs pim.gemv")
