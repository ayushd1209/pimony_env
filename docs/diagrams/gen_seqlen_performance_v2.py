#!/usr/bin/env python3
"""CPU-only GPT-3 token sweep, two matching panels in the thesis figure style (figstyle.py).

Same data, labels and decode references as gen_seqlen_excalidraw.py --performance:
(a) cycles per processed token, (b) multiply-adds per byte read from DRAM, against prefill
prompt length on a log2 axis. Both panels share one frame so they align as (a) and (b).
Run: python3 gen_seqlen_performance_v2.py [--svg-only]
  ->  seqlen_performance_a_v2.{svg,pdf,png}, seqlen_performance_b_v2.{svg,pdf,png}
"""
import math, os
from figstyle import *

TOKENS = [4, 16, 64, 128, 256, 512]
CYCLES_PER_TOKEN = [44959560, 13563564, 5913266, 4754784, 4605617, 4462854]
DECODE_CYCLES = 27401792                      # one new token with 256 cached tokens
REUSE = [1.57, 5.95, 17.09, 24.14, 19.06, 18.61]
DECODE_REUSE = 0.49

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
save(os.path.join(HERE, "seqlen_performance_a_v2"), X0, 0, W, H, "Cycles per processed token (millions)")

x_of, y_of = panel("b", ["Multiply-adds per byte", "read from DRAM"], REUSE,
                   ["1.6", "6.0", "17.1", "24.1", "19.1", "18.6"], 28, 5, DECODE_REUSE, "0.49")
x256 = x_of(256)
line(x256, y_of(REUSE[4]) + 9, x256, y_of(11.5), 1.0, BLUE)               # 256 tokens vs decode
ctext(x256, y_of(9.4), "256 tokens:", 16, "sans", 400, BLUE)
ctext(x256, y_of(9.4) + 20, f"{round(REUSE[4] / DECODE_REUSE)}× decode", 16, "sans", 500, BLUE)
save(os.path.join(HERE, "seqlen_performance_b_v2"), X0, 0, W, H, "Multiply-adds per byte read from DRAM")
