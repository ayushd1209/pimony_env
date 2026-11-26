#!/usr/bin/env python3
"""One GPT-3 decoder layer to scale, all heads on CPU vs all heads on PIM (thesis 6.6), thesis figure style.

Same data, checks and labels as gen_layer_gantt.py (figstyle.py look).
Two rows on one time axis, two lanes each. CPU: awake charcoal, asleep in pim.wait solid blue,
summed per section (awake first). PIM: busy (blue stripes) while the CPU sleeps, idle (white) elsewhere.
Sections = the eight PHASE() blocks of gpt3.c, named as in the thesis (W_o = W with subscript o).
Data: m5out/gpt3_hs2_n0_o3 and gpt3_hs2_n32_o3; cycles = simTicks / 333,
asleep = cycles - numCycles (= quiesceCycles). Checked against stats.txt when it is there.
Run: python3 gen_layer_gantt_v2.py [--svg-only]  ->  layer_gantt_v2.{svg,pdf,png}
"""
import os
from figstyle import *

# ---------------------------------------------------------------- data
SECTIONS = ["LN 1 + QKV", "attention", "W_o", "residual 1 + LN 2", "W_1", "GELU", "W_2", "residual 2"]
# per section (total cycles, asleep cycles)
RUNS = [
    ("All heads on CPU", "gpt3_hs2_n0_o3", [(390232, 235878), (1934445, 0), (158731, 82726), (9620, 0),
                                            (495833, 330870), (35053, 0), (569122, 316913), (1945, 0)]),
    ("All heads on PIM", "gpt3_hs2_n32_o3", [(380013, 226783), (1073518, 95467), (157881, 84409), (7292, 0),
                                             (499249, 335074), (35097, 0), (568225, 313746), (2765, 0)]),
]
PERIOD = 333                           # system.clk_domain.clock, ticks

def check(run, rows):
    """Recompute from stats.txt blocks 1-8 when the run is on disk."""
    f = os.path.expanduser(f"~/pimony_env/gem5/m5out/{run}/stats.txt")
    if not os.path.exists(f):
        warn.append(f"not checked: {f} missing")
        return
    b, t, n = 0, {}, {}
    for l in open(f):
        if "Begin Simulation Statistics" in l:
            b += 1
        elif 1 <= b <= 8 and l.split()[:1] == ["simTicks"]:
            t[b] = int(l.split()[1])
        elif 1 <= b <= 8 and l.split()[:1] == ["system.cpu.numCycles"]:
            n[b] = int(l.split()[1])
    for i, (tot, sl) in enumerate(rows, 1):
        assert t[i] % PERIOD == 0 and t[i] // PERIOD == tot, (run, i, t[i])
        assert tot - n[i] == sl, (run, i, n[i])

for _, run, rows in RUNS:
    check(run, rows)

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

save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "layer_gantt_v2"),
     4, -4, 940, LY + 20 + 14, "One decoder layer, CPU vs PIM heads")
