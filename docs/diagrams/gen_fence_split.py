#!/usr/bin/env python3
"""Cost of pim.fence for one GPT-3 layer (thesis Section 6.5).

Matmul blocks 1, 3, 5, 7 (QKV, W_o, W_1, W_2), cycles = simTicks / 333.
Each host bar splits total time into asleep, other awake work, and fence cost.
Fence cost is the total with fences minus the total of the matching control.
Checks stats.txt when present, using the Section 6.3 figure's writer and helpers.
Run: python3 gen_fence_split.py -> fence_split.excalidraw
"""
import json, itertools, os

SANS = 2                               # Excalidraw: Helvetica
INK, GREY, WHITE = "#1e1e1e", "#495057", "#ffffff"
PIM_ST = "#1864ab"
REGION = "#868e96"
CPU_ON = GREY                          # awake: solid
FENCES = "#e8590c"
GRID = "#dee2e6"
NONE = "transparent"

# Helvetica advance widths (AFM, per 1000 em) for the overflow check
_HELV = dict(zip(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~",
    [278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278]+[556]*10+
    [278,278,584,584,584,556,1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,667,778,722,667,611,
     722,667,944,667,667,611,278,278,278,469,556,333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,
     556,556,333,500,278,556,500,722,500,500,500,334,260,334,584]))

def text_w(s, size):
    return max(sum(_HELV.get(c, 1000) for c in l) for l in s.split("\n")) / 1000 * size

_ids = itertools.count(1)
els, warnings = [], []

def _base(kind, x, y, w, h, **kw):
    n = next(_ids)
    e = {"id": f"{kind[:2]}{n}", "type": kind, "x": x, "y": y, "width": w, "height": h,
         "angle": 0, "strokeColor": INK, "backgroundColor": "transparent",
         "fillStyle": "solid", "strokeWidth": 1, "strokeStyle": "solid",
         "roughness": 0, "opacity": 100, "groupIds": [], "frameId": None,
         "roundness": None, "seed": n * 7919 % 2147483647, "version": 1,
         "versionNonce": n * 104729 % 2147483647, "isDeleted": False,
         "boundElements": [], "updated": 1, "link": None, "locked": False}
    e.update(kw)
    els.append(e)
    return e

def rect(x, y, w, h, st, bg, fill="solid", dash="solid", sw=1):
    return _base("rectangle", x, y, w, h, strokeColor=st, backgroundColor=bg,
                 fillStyle=fill, strokeStyle=dash, strokeWidth=sw)

def line(pts, st=REGION, sw=1, dash="solid"):
    x0, y0 = pts[0]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return _base("line", x0, y0, max(xs) - min(xs), max(ys) - min(ys), strokeColor=st,
                 strokeWidth=sw, strokeStyle=dash, points=[[px - x0, py - y0] for px, py in pts],
                 lastCommittedPoint=None, startBinding=None, endBinding=None,
                 startArrowhead=None, endArrowhead=None)

def text(x, y, s, size=14, color=INK, align="left", box=None):
    """x is the left edge (align=left), the centre (center) or the right edge (right)."""
    w = text_w(s, size)
    h = size * 1.25 * len(s.split("\n"))
    left = {"left": x, "center": x - w / 2, "right": x - w}[align]
    if box and (left < box[0] - 0.5 or left + w > box[1] + 0.5):
        warnings.append(f"overflow: {s!r} [{left:.0f}..{left + w:.0f}] not in [{box[0]:.0f}..{box[1]:.0f}]")
    return _base("text", left, y, w, h, strokeColor=color, fontSize=size, fontFamily=SANS,
                 text=s, originalText=s, textAlign=align, verticalAlign="top",
                 containerId=None, lineHeight=1.25, baseline=round(size * 0.9), autoResize=True)

# ---------------------------------------------------------------- data
BLOCKS = [1, 3, 5, 7]                  # PHASE() blocks: QKV, W_o, W_1, W_2
PERIOD = 333                          # CPU clock period, ticks
# label, run with fences, control, total, asleep, control total, fence cost
HOSTS = [("MinorCPU", "gpt3_scr64_minor", "gpt3_scr64_nfd_minor",
          2285603, 952950, 2162209, 123394),
         ("O3CPU", "gpt3_scr64_o3", "gpt3_scr64_nf_o3",
          1584037, 959983, 1308908, 275129)]

def check_run(run, tot, sl=None):
    """Recompute total and optional asleep from stats.txt blocks 1, 3, 5, 7."""
    f = os.path.expanduser(f"~/pimony_env/gem5/m5out/{run}/stats.txt")
    if not os.path.exists(f):
        warnings.append(f"not checked: {f} missing")
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
LBL = 18                               # all text; >= 9 pt at 0.85\linewidth (checked below)
TEXTWIDTH_PT = 415                     # conservative thesis \textwidth, as in Section 6.3
X0, TOP, PH = 60, 40, 340              # y axis, top of plot (2.5 M), plot height
YMAX = 2.5e6
PXM = PH / YMAX                        # px per cycle
BASE = TOP + PH
BW, PAD, GAP = 80, 34, 124             # space between bars includes the MinorCPU fence label
XH = [X0 + PAD + k * (BW + GAP) for k in range(2)]
XR = XH[-1] + BW + PAD
LH = LBL * 1.25
COLOURS = {"asleep": PIM_ST, "other": CPU_ON, "fences": FENCES}

def y_of(c):
    return BASE - c * PXM

def block(x, lo, hi, kind):
    """No outlines, so each fill ends exactly on its value."""
    rect(x, y_of(hi), BW, (hi - lo) * PXM, NONE, COLOURS[kind])

# ---- grid and axis
for k in range(6):
    y = y_of(k * 0.5e6)
    if k:
        line([(X0, y), (XR, y)], GRID)
    line([(X0 - 5, y), (X0, y)], INK)
    text(X0 - 8, y - LH / 2, "0" if k == 0 else f"{k / 2:.1f}", LBL, align="right")
text(X0 - 8 - text_w("2.5", LBL), TOP - LH - 12, "million CPU cycles", LBL)

# ---- bars
for i, ((name, _, _, tot, sl, _, fence), x) in enumerate(zip(HOSTS, XH)):
    other = tot - sl - fence
    block(x, 0, sl, "asleep")
    block(x, sl, tot - fence, "other")
    block(x, tot - fence, tot, "fences")
    text(x + BW / 2, (y_of(tot - fence) + y_of(sl)) / 2 - LH / 2 + 1,
         f"{other / 1e6:.2f}", LBL, WHITE, "center", box=(x, x + BW))
    text(x + BW / 2, y_of(tot) - LH - 3, f"{tot / 1e6:.2f}", LBL, align="center")
    text(x + BW / 2, BASE + 8, name, LBL, align="center")
    label = f"{fence / 1e6:.2f} ({100 * fence / tot:.1f}%)"
    right = XH[i + 1] - 12 if i + 1 < len(XH) else x + BW + 12 + text_w(label, LBL)
    text(x + BW + 12, (y_of(tot) + y_of(tot - fence)) / 2 - LH / 2,
         label, LBL, box=(x + BW + 12, right))
line([(X0, TOP), (X0, BASE), (XR, BASE)], INK)

# ---- legend to the right, below the fence callouts
LX, LY = XR + 24, 225
items = [("asleep", "CPU asleep in pim.wait\n(PIM busy)", 0),
         ("other", "CPU awake: other work", 60),
         ("fences", "CPU awake: fences", 94)]
for kind, name, dy in items:
    rect(LX, LY + dy, 30, 20, NONE, COLOURS[kind])
    text(LX + 38, LY + dy - 1, name, LBL)

# ---- text size at 0.85\linewidth (export_pdf.py pads 10 px each side)
x_lo = min(e["x"] for e in els)
x_hi = max(e["x"] + e["width"] for e in els)
pt = LBL * 0.85 * TEXTWIDTH_PT / (x_hi - x_lo + 20)
if pt < 9:
    warnings.append(f"text {pt:.1f} pt at 0.85\\linewidth, below 9 pt")

# ---------------------------------------------------------------- write
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fence_split.excalidraw")
json.dump({"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
           "elements": els, "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
           "files": {}}, open(out, "w"), indent=1)
print(f"wrote {out}: {len(els)} elements, {x_hi - x_lo + 20:.0f} px wide, text {pt:.1f} pt at 0.85\\linewidth")
for name, run, control, tot, sl, ctrl, fence in HOSTS:
    print(f"{name:16s} total {tot:>9,}  asleep {sl:>7,}  other {tot - sl - fence:>9,}  "
          f"fences {fence:>7,} ({100 * fence / tot:.1f}%)  ({run})")
    print(f"{'control':16s} total {ctrl:>9,}  ({control})")
for w in warnings:
    print("WARN", w)
