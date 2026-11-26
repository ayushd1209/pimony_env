#!/usr/bin/env python3
"""GPT-3 decode matmul time: pim.gemv vs pim.dispatch (sec:eval:granularity), thesis figure style.

Same data, checks and labels as gen_gemv_vs_dispatch.py (figstyle.py look).
Matmul blocks 1, 3, 5, 7 (QKV, W_o, W_1, W_2), CPU asleep then CPU awake.
Cycles = simTicks / 333; asleep = cycles - numCycles (= quiesceCycles).
PIMony's same-jobs reference applies only to pim.gemv: dispatch omits buffer
fill, readout and write-back. Checked against stats.txt and aligned PIMony logs.
Solid blue = CPU asleep in pim.wait (PIM busy), charcoal = CPU awake.
Run: python3 gen_gemv_vs_dispatch_v2.py [--svg-only]  ->  gemv_vs_dispatch_v2.{svg,pdf,png}
"""
import os
from figstyle import *

# ---------------------------------------------------------------- data
BLOCKS = [1, 3, 5, 7]                  # PHASE() blocks: QKV, W_o, W_1, W_2
# (label, run, total cycles, asleep cycles)
RUNS = [("pim.gemv",     "gpt3_scr64_o3",          1584037, 959983),
        ("pim.dispatch", "gpt3_disp_w32_scr64_o3", 2844295, 730362)]
PIMONY_OURS = 941105                   # pimony_baseline/RESULTS.md sec. 3
PERIOD, TCK = 333, 937                 # CPU / DRAM clock period, ticks
LOGS = os.path.expanduser("~/pimony_env/gem5/configs/scratch/progs/pimony_baseline/logs")

def check_run(run, tot, sl):
    """Recompute from stats.txt blocks 1, 3, 5, 7 when the run is on disk."""
    f = os.path.expanduser(f"~/pimony_env/gem5/m5out/{run}/stats.txt")
    if not os.path.exists(f):
        warn.append(f"not checked: {f} missing")
        return
    b, t, n, q = 0, {}, {}, {}
    for l in open(f):
        if "Begin Simulation Statistics" in l:
            b += 1
        elif b in BLOCKS and l.split()[:1] == ["simTicks"]:
            t[b] = int(l.split()[1])
        elif b in BLOCKS and l.split()[:1] == ["system.cpu.numCycles"]:
            n[b] = int(l.split()[1])
        elif b in BLOCKS and l.split()[:1] == ["system.cpu.quiesceCycles"]:
            q[b] = int(l.split()[1])
    assert all(t[i] % PERIOD == 0 for i in BLOCKS), (run, t)
    assert sum(t[i] // PERIOD for i in BLOCKS) == tot, (run, t)
    assert sum(t[i] // PERIOD - n[i] for i in BLOCKS) == sl, (run, n)
    assert all(t[i] // PERIOD - n[i] == q[i] for i in BLOCKS), (run, q)

def check_pimony(sub, prefix, cyc):
    """Sum of 'GEMV done latency' over the four matmul logs, DRAM -> CPU cycles."""
    fs = [f"{LOGS}/{sub}/{prefix}_{m}.log" for m in ("qkv", "wo", "w1", "w2")]
    if not all(os.path.exists(f) for f in fs):
        warn.append(f"not checked: {LOGS}/{sub}/{prefix}_*.log missing")
        return
    dram = sum(int(l.split()[-2]) for f in fs for l in open(f) if "GEMV done latency" in l)
    assert round(dram * TCK / PERIOD) == cyc, (sub, dram)

for _, run, tot, sl in RUNS:
    check_run(run, tot, sl)
check_pimony("aligned", "seq", PIMONY_OURS)

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

save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gemv_vs_dispatch_v2"),
     round(left - 12), 0, round(right - left + 24), LY + 32 + 20 + 12, "pim.gemv vs pim.dispatch")
for name, run, tot, sl in RUNS:
    print(f"{name:16s} total {tot:>10,}  asleep {sl:>10,}  awake {tot - sl:>10,}  ({run})")
print(f"{'PIMony, our jobs':16s} dashed {PIMONY_OURS:>9,}  (pim.gemv only)")
