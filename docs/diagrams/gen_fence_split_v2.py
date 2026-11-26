#!/usr/bin/env python3
"""Cost of pim.fence for one GPT-3 layer (thesis Section 6.5), thesis figure style (figstyle.py).

Same data, checks and labels as gen_fence_split.py, with the y title as in the edited
fence_split.pdf. Matmul blocks 1, 3, 5, 7 (QKV, W_o, W_1, W_2), cycles = simTicks / 333.
Each host bar splits total time into asleep, other awake work, and fence cost.
Fence cost is the total with fences minus the total of the matching control.
Colours mean the same in every graph: solid blue = CPU asleep, charcoal = CPU awake,
orange = our instruction (here: fences).
Run: python3 gen_fence_split_v2.py [--svg-only]  ->  fence_split_v2.{svg,pdf,png}
"""
import os
from figstyle import *

# ---------------------------------------------------------------- data
BLOCKS = [1, 3, 5, 7]                  # PHASE() blocks: QKV, W_o, W_1, W_2
PERIOD = 333                           # CPU clock period, ticks
# label, run with fences, control, total, asleep, control total, fence cost
HOSTS = [("MinorCPU", "gpt3_scr64_minor", "gpt3_scr64_nfd_minor",
          2285603, 952950, 2162209, 123394),
         ("O3CPU", "gpt3_scr64_o3", "gpt3_scr64_nf_o3",
          1584037, 959983, 1308908, 275129)]

def check_run(run, tot, sl=None):
    """Recompute total and optional asleep from stats.txt blocks 1, 3, 5, 7."""
    f = os.path.expanduser(f"~/pimony_env/gem5/m5out/{run}/stats.txt")
    if not os.path.exists(f):
        warn.append(f"not checked: {f} missing")
        return
    b, t, n = 0, {}, {}
    with open(f) as stats:
        for l in stats:
            if "Begin Simulation Statistics" in l:
                b += 1
            elif b in BLOCKS and l.split()[:1] == ["simTicks"]:
                t[b] = int(l.split()[1])
            elif b in BLOCKS and l.split()[:1] == ["system.cpu.numCycles"]:
                n[b] = int(l.split()[1])
    assert all(i in t for i in BLOCKS), (run, "missing simTicks", t)
    assert all(t[i] % PERIOD == 0 for i in BLOCKS), (run, t)
    actual = sum(t[i] // PERIOD for i in BLOCKS)
    assert actual == tot, (run, "total", actual, "expected", tot)
    if sl is not None:
        assert all(i in n for i in BLOCKS), (run, "missing numCycles", n)
        actual_sl = sum(t[i] // PERIOD - n[i] for i in BLOCKS)
        assert actual_sl == sl, (run, "asleep", actual_sl, "expected", sl)

for _, run, control, tot, sl, ctrl, fence in HOSTS:
    check_run(run, tot, sl)
    check_run(control, ctrl)
    assert tot - ctrl == fence, (run, "fence cost", tot - ctrl, "expected", fence)
    assert tot - sl - fence >= 0, (run, "negative other work")

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

save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fence_split_v2"),
     0, 0, round(LX + max(widths) + 12), BASE + 40, "Cost of pim.fence")
for name, run, control, tot, sl, ctrl, fence in HOSTS:
    print(f"{name:16s} total {tot:>9,}  asleep {sl:>7,}  other {tot - sl - fence:>9,}  "
          f"fences {fence:>7,} ({100 * fence / tot:.1f}%)  ({run})")
