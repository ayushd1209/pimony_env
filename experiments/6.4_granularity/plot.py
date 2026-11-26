#!/usr/bin/env python3
"""skip: plumbing. Thesis Figure 6.3 from fig6_3.csv (written by parse.py), thesis figure style.

GPT-3 decode matmul time on O3 (blocks 1, 3, 5, 7): pim.gemv vs pim.dispatch with batches of 32,
CPU asleep in pim.wait (solid blue) then CPU awake (charcoal). The dashed line is PIMony running
pim.gemv's jobs alone; it applies only to pim.gemv, since dispatch omits buffer fills, readout
and write-back. Reproduces the thesis figure byte-for-byte (SVG).
Usage: python3 experiments/6.4_granularity/plot.py [--svg-only]  ->  fig6_3.{svg,pdf,png} next to this script
"""
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../../docs/diagrams"))
from figstyle import *  # noqa: E402,F403

# ---------------------------------------------------------------- data
rows = {r["bar"]: r for r in csv.DictReader(open(os.path.join(HERE, "fig6_3.csv")))}
# (label, run, total cycles, asleep cycles)
RUNS = [(b, rows[b]["source"], int(rows[b]["cycles"]), int(rows[b]["asleep_cycles"]))
        for b in ("pim.gemv", "pim.dispatch")]
PIMONY_OURS = int(rows["pimony_same_jobs"]["cycles"])

# ---------------------------------------------------------------- layout
LBL = 18
X0, XR = 220, 760                      # plot spans 0 to 3.0 M cycles
TOP, BH, GAP = 42, 36, 28              # bar top, height, gap between bars
YB = [TOP, TOP + BH + GAP]
BASE = YB[-1] + BH + 12                # bottom x axis
XMAX = 3.0e6
PXM = (XR - X0) / XMAX                 # px per CPU cycle

def x_of(c):
    return X0 + c * PXM

def block(y, lo, hi, kind):
    """Fill lo..hi cycles, label inside if it fits, otherwise just above."""
    x, w = x_of(lo), (hi - lo) * PXM
    seg(x, y, w, BH, kind)
    label = f"{(hi - lo) / 1e6:.2f}"
    if tw(label, LBL, "sans", 500) + 16 <= w:
        if kind == "sleep":
            rich(x + w / 2, y + BH / 2, label, LBL, 500, fill=WHITE)
        else:
            ctext(x + w / 2, y + BH / 2, label, LBL, "sans", 500, WHITE)
    else:
        ctext(x + w / 2, y - 14, label, LBL, "sans", 500)

# ---- grid and axis
for k in range(7):
    x = x_of(k * 0.5e6)
    if k:
        out.append(f'<path d="M{x:.2f} {TOP - 12}V{BASE}" stroke="{GRID}" stroke-width="1"/>')
    line(x, BASE, x, BASE + 5, SW)
    ctext(x, BASE + 20, "0" if k == 0 else f"{k / 2:.1f}", LBL)
ctext((X0 + XR) / 2, BASE + 52, "million CPU cycles", LBL, "sans", 500)

# ---- bars and annotations
for (name, _, tot, sl), y in zip(RUNS, YB):
    assert 0 <= sl <= tot <= XMAX, (name, tot, sl)
    block(y, 0, sl, "sleep")
    block(y, sl, tot, "awake")
    if name == "pim.dispatch":
        rich(X0 - 16, y + BH / 2 - 11, f"`{name}`", LBL, anchor="end")
        ctext(X0 - 16, y + BH / 2 + 12, "32 commands, then wait", LBL, "sans", 400, SUB, "end")
    else:
        rich(X0 - 16, y + BH / 2, f"`{name}`", LBL, anchor="end")
    ctext(x_of(tot) + 8, y + BH / 2, f"{tot / 1e6:.2f}", LBL, "sans", 500, anchor="start")
line(X0, TOP - 12, X0, BASE, SW_BOX)
line(X0, BASE, XR, BASE, SW_BOX)
# Same jobs only for pim.gemv; dispatch runs do less PIM work.
REF_TOP, REF_BOTTOM = YB[0] - 10, YB[0] + BH + 10
assert REF_BOTTOM < YB[1]
xr = x_of(PIMONY_OURS)
out.append(f'<path d="M{xr:.2f} {REF_TOP}V{REF_BOTTOM}" stroke="{INK}" stroke-width="1.6" stroke-dasharray="6 4"/>')
ctext(xr, REF_TOP - 13, f"{PIMONY_OURS / 1e6:.2f}", LBL, "sans", 500)

# ---- legend: two columns below the axis
LY = BASE + 82
items = [("sleep", "CPU asleep in `pim.wait` (PIM busy)"), ("awake", "CPU awake"),
         ("dash", "PIMony alone, same jobs")]
cols = [max(40 + rich_w(n, LBL) for _, n in items[c::2]) for c in (0, 1)]
left = X0 - 16 - tw("32 commands, then wait", LBL)
right = max(XR + 14, max(x_of(tot) + 8 + tw(f"{tot / 1e6:.2f}", LBL, "sans", 500) for _, _, tot, _ in RUNS))
lx0 = (left + right) / 2 - (cols[0] + 36 + cols[1]) / 2
for i, (kind, name) in enumerate(items):
    key(lx0 + (i % 2) * (cols[0] + 36), LY + (i // 2) * 32, kind, name, LBL)

save(os.path.join(HERE, "fig6_3"),
     round(left - 12), 0, round(right - left + 24), LY + 32 + 20 + 12, "pim.gemv vs pim.dispatch")
