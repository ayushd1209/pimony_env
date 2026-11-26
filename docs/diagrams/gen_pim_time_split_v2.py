#!/usr/bin/env python3
"""Matmul time of one GPT-3 2.7B decode layer, PIMony alone vs pim.gemv on three hosts (thesis 6.2), thesis figure style.

Same data, checks and labels as gen_pim_time_split.py, with the y title and host names
as in the edited pim_time_split.pdf. Matmul blocks 1, 3, 5, 7 (QKV, W_o, W_1, W_2).
PIMony alone: its own GEMV workload, no CPU (pimony_baseline/RESULTS.md sec. 1, sum row).
Hosts: gpt3_scr64_{minor,o3,o3v61}; cycles = simTicks / 333, asleep = cycles - numCycles.
Dashed: PIMony running our jobs (RESULTS.md sec. 3); PIMony cycles = DRAM cycles x 937 / 333.
Solid blue = CPU asleep in pim.wait, blue stripes = PIM busy with no CPU, charcoal = CPU awake.
Run: python3 gen_pim_time_split_v2.py [--svg-only]  ->  pim_time_split_v2.{svg,pdf,png}
"""
import os
from figstyle import *

# ---------------------------------------------------------------- data
BLOCKS = [1, 3, 5, 7]                  # PHASE() blocks of gpt3.c: QKV, W_o, W_1, W_2
# host bars: (label, sub-label, run, total cycles, asleep cycles)
HOSTS = [("MinorCPU", "",               "gpt3_scr64_minor", 2285603, 952950),
         ("O3CPU",    "(4 SIMD units)", "gpt3_scr64_o3",    1584037, 959983),
         ("O3CPU",    "(1 SIMD unit)",  "gpt3_scr64_o3v61", 1586830, 959683)]
PIMONY_ALONE = 705145                  # RESULTS.md sec. 1, sum row (PIMony as shipped)
PIMONY_OURS = 941105                   # RESULTS.md sec. 3, "PIMony, our work"
PERIOD, TCK = 333, 937                 # CPU / DRAM clock period, ticks
LOGS = os.path.expanduser("~/pimony_env/gem5/configs/scratch/progs/pimony_baseline/logs")

def check_run(run, tot, sl):
    """Recompute from stats.txt blocks 1, 3, 5, 7 when the run is on disk."""
    f = os.path.expanduser(f"~/pimony_env/gem5/m5out/{run}/stats.txt")
    if not os.path.exists(f):
        warn.append(f"not checked: {f} missing")
        return
    b, t, n = 0, {}, {}
    for l in open(f):
        if "Begin Simulation Statistics" in l:
            b += 1
        elif b in BLOCKS and l.split()[:1] == ["simTicks"]:
            t[b] = int(l.split()[1])
        elif b in BLOCKS and l.split()[:1] == ["system.cpu.numCycles"]:
            n[b] = int(l.split()[1])
    assert all(t[i] % PERIOD == 0 for i in BLOCKS), (run, t)
    assert sum(t[i] // PERIOD for i in BLOCKS) == tot, (run, t)
    assert sum(t[i] // PERIOD - n[i] for i in BLOCKS) == sl, (run, n)

def check_pimony(sub, prefix, cyc):
    """Sum of 'GEMV done latency' over the four matmul logs, DRAM -> CPU cycles."""
    fs = [f"{LOGS}/{sub}/{prefix}_{m}.log" for m in ("qkv", "wo", "w1", "w2")]
    if not all(os.path.exists(f) for f in fs):
        warn.append(f"not checked: {LOGS}/{sub}/{prefix}_*.log missing")
        return
    dram = sum(int(l.split()[-2]) for f in fs for l in open(f) if "GEMV done latency" in l)
    assert round(dram * TCK / PERIOD) == cyc, (sub, dram)

for _, _, run, tot, sl in HOSTS:
    check_run(run, tot, sl)
check_pimony("standalone", "gpt3", PIMONY_ALONE)
check_pimony("aligned", "seq", PIMONY_OURS)

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

save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "pim_time_split_v2"),
     0, 0, round(max(XR + 12, lx0 + cols[0] + 36 + cols[1] + 12)), LY + 32 + 20 + 12,
     "Matmul time, PIMony alone vs pim.gemv")
print(f"{'PIMony alone':16s} total {PIMONY_ALONE:>9,}")
for name, sub, run, tot, sl in HOSTS:
    print(f"{name + ' ' + sub:16s} total {tot:>9,}  asleep {sl:>7,}  awake {tot - sl:>9,}  ({run})")
print(f"{'PIMony, our jobs':16s} dashed {PIMONY_OURS:>8,}")
