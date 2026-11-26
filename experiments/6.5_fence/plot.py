#!/usr/bin/env python3
"""skip: plumbing. Thesis Figure 6.4 from fig6_4.csv (written by parse.py), thesis figure style.

GPT-3 matmul time (blocks 1, 3, 5, 7) per CPU, split into CPU asleep (solid blue), other awake
work (charcoal) and fence cost (orange) = total with fences minus total of the fence-free control.
Reproduces the thesis figure byte-for-byte (SVG).
Usage: python3 experiments/6.5_fence/plot.py [--svg-only]  ->  fig6_4.{svg,pdf,png} next to this script
"""
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../../docs/diagrams"))
from figstyle import *  # noqa: E402,F403

# ---------------------------------------------------------------- data
LABELS = {"minor": "MinorCPU", "o3": "O3CPU"}
rows = [r for r in csv.DictReader(open(os.path.join(HERE, "fig6_4.csv")))]
# label, run with fences, control, total, asleep, control total, fence cost
HOSTS = [(LABELS[r["bar"]], r["run"], r["control"], int(r["cycles"]), int(r["asleep_cycles"]),
          int(r["control_cycles"]), int(r["fence_cycles"])) for r in rows]

# ---------------------------------------------------------------- layout
LBL = 18
X0, TOP, PH = 96, 40, 340              # y axis, top of plot (2.5 M), plot height
YMAX = 2.5e6
PXM = PH / YMAX                        # px per cycle
BASE = TOP + PH
BW, PAD, GAP = 80, 34, 124             # space between bars includes the MinorCPU fence label
XH = [X0 + PAD + k * (BW + GAP) for k in range(2)]
XR = XH[-1] + BW + PAD

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
for i, ((name, _, _, tot, sl, _, fence), x) in enumerate(zip(HOSTS, XH)):
    other = tot - sl - fence
    seg(x, y_of(sl), BW, sl * PXM, "sleep")
    seg(x, y_of(tot - fence), BW, other * PXM, "awake")
    seg(x, y_of(tot), BW, fence * PXM, "fence")
    ctext(x + BW / 2, (y_of(tot - fence) + y_of(sl)) / 2, f"{other / 1e6:.2f}", LBL, "sans", 500, WHITE,
          box=(x, x + BW))
    ctext(x + BW / 2, y_of(tot) - 15, f"{tot / 1e6:.2f}", LBL, "sans", 500)
    ctext(x + BW / 2, BASE + 20, name, LBL)
    label = f"{fence / 1e6:.2f} ({100 * fence / tot:.1f}%)"
    right = XH[i + 1] - 12 if i + 1 < len(XH) else XR + 400
    ctext(x + BW + 10, (y_of(tot) + y_of(tot - fence)) / 2, label, LBL, anchor="start",
          box=(x + BW + 10, right))
line(X0, TOP - 6, X0, BASE, SW_BOX)
line(X0, BASE, XR, BASE, SW_BOX)

# ---- legend to the right, below the fence callouts
LX, LY = XR + 24, 225
widths = [key(LX, LY, "sleep", "CPU asleep in `pim.wait`\n(PIM busy)", LBL),
          key(LX, LY + 60, "awake", "CPU awake: other work", LBL),
          key(LX, LY + 94, "fence", "CPU awake: fences", LBL)]

save(os.path.join(HERE, "fig6_4"),
     0, 0, round(LX + max(widths) + 12), BASE + 40, "Cost of pim.fence")
