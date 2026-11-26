#!/usr/bin/env python3
"""skip: plumbing. Thesis Figure 6.1 (a) and (b) from fig6_1.csv (written by parse.py), thesis figure style.

CPU-only GPT-3 layer on O3: (a) cycles per processed token, (b) multiply-adds per byte read from DRAM,
against prefill prompt length on a log2 axis, with decode as the dashed reference.
Reproduces both thesis panels byte-for-byte (SVG).
Usage: python3 experiments/6.2_seqlen/plot.py [--svg-only]  ->  fig6_1a.{svg,pdf,png}, fig6_1b.{svg,pdf,png}
"""
import csv
import math, os, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../docs/diagrams"))
from figstyle import *  # noqa: E402,F403

rows = list(csv.DictReader(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fig6_1.csv"))))
prefill = [r for r in rows if r["kind"] == "prefill"]
decode = next(r for r in rows if r["kind"] == "decode")
TOKENS = [int(r["tokens"]) for r in prefill]
CYCLES_PER_TOKEN = [int(r["cycles_per_token"]) for r in prefill]
DECODE_CYCLES = int(decode["cycles_per_token"])           # one new token with 256 cached tokens
REUSE = [float(r["macs_per_byte"]) for r in prefill]
DECODE_REUSE = float(decode["macs_per_byte"])

GRID, REF = "#e2e2e2", "#555555"
X0, W, H = 66, 900, 380                      # same frame for both panels
PL, PR, PT, PB = 150, 950, 34, 282            # plot area
LO, HI = 1.55, 9.45                           # log2 axis range

def x_of(t):
    return PL + (math.log2(t) - LO) / (HI - LO) * (PR - PL)

def panel(name, title, values, labels, ymax, step, decode, decode_label, beside=()):
    y_of = lambda v: PB - v / ymax * (PB - PT)
    for v in range(0, int(ymax) + 1, step):                               # grid + y ticks
        if v:
            out.append(f'<path d="M{PL} {y_of(v):.2f}H{PR}" stroke="{GRID}" stroke-width="1"/>')
        line(PL - 5, y_of(v), PL, y_of(v), SW)
        ctext(PL - 10, y_of(v), str(v), 17, anchor="end")
    for t in TOKENS:                                                      # x ticks
        line(x_of(t), PB, x_of(t), PB + 5, SW)
        ctext(x_of(t), PB + 22, str(t), 17)
    line(PL, PT - 6, PL, PB, SW_BOX)                                      # axes
    line(PL, PB, PR, PB, SW_BOX)
    for i, s in enumerate(title):                                         # y title, reads upward
        ctext(86 + 23 * i, (PT + PB) / 2, s, 17.5, "sans", 500, rot=-90)
    ctext((PL + PR) / 2, PB + 62, "Prefill prompt length (tokens; log scale, base 2)", 17.5, "sans", 500)

    yr = y_of(decode)                                                     # fixed decode reference
    out.append(f'<path d="M{PL} {yr:.2f}H{PR}" stroke="{REF}" stroke-width="1.4" stroke-dasharray="6 4"/>')
    ctext(PR - 2, yr - 12, f"Decode reference: {decode_label}", 16, "sans", 400, SUB, "end")

    pts = [(x_of(t), y_of(v)) for t, v in zip(TOKENS, values)]           # prefill measurements
    out.append(f'<path d="M{"L".join(f"{x:.2f} {y:.2f}" for x, y in pts)}" stroke="{BLUE}" '
               f'stroke-width="2.2" fill="none" stroke-linejoin="round"/>')
    for i, ((x, y), lab) in enumerate(zip(pts, labels)):
        out.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="4.8" fill="{BLUE}" stroke="{WHITE}" stroke-width="1.6"/>')
        if i in beside:                                                   # steep segment: label to the right
            ctext(x + 10, y - 11, lab, 16.5, "sans", 500, BLUE, anchor="start")
        else:
            ctext(x, y - 16, lab, 16.5, "sans", 500, BLUE)
    return x_of, y_of

HERE = os.path.dirname(os.path.abspath(__file__))
costs = [c / 1e6 for c in CYCLES_PER_TOKEN]
panel("a", ["Cycles per processed", "token (millions)"], costs, [f"{c:.1f}" for c in costs],
      50, 10, DECODE_CYCLES / 1e6, f"{DECODE_CYCLES / 1e6:.1f}", beside=(1,))
save(os.path.join(HERE, "fig6_1a"), X0, 0, W, H, "Cycles per processed token (millions)")

x_of, y_of = panel("b", ["Multiply-adds per byte", "read from DRAM"], REUSE,
                   [f"{r:.1f}" for r in REUSE], 28, 5, DECODE_REUSE, f"{DECODE_REUSE:.2f}")
x256 = x_of(256)
line(x256, y_of(REUSE[4]) + 9, x256, y_of(11.5), 1.0, BLUE)               # 256 tokens vs decode
ctext(x256, y_of(9.4), "256 tokens:", 16, "sans", 400, BLUE)
ctext(x256, y_of(9.4) + 20, f"{round(REUSE[4] / DECODE_REUSE)}× decode", 16, "sans", 500, BLUE)
save(os.path.join(HERE, "fig6_1b"), X0, 0, W, H, "Multiply-adds per byte read from DRAM")
