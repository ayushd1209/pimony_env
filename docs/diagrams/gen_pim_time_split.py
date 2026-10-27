#!/usr/bin/env python3
"""Matmul time of one GPT-3 2.7B decode layer, PIMony alone vs pim.gemv on three hosts (thesis 6.2, sec:eval:speedup).

Matmul blocks 1, 3, 5, 7 (QKV, W_o, W_1, W_2), one stacked bar each, heights to scale.
PIMony alone: its own GEMV workload, no CPU (pimony_baseline/RESULTS.md sec. 1, sum row).
Hosts: gpt3_scr64_{minor,o3,o3v61}; cycles = simTicks / 333 (clock period 333 ps),
asleep = cycles - numCycles (= quiesceCycles), awake = cycles - asleep.
Dashed: PIMony running our jobs (RESULTS.md sec. 3); PIMony cycles = DRAM cycles x 937 / 333.
Checked against stats.txt and the PIMony logs when they are there.
Run: python3 gen_pim_time_split.py  ->  pim_time_split.excalidraw
"""
import json, itertools, os

SANS = 2                               # Excalidraw: Helvetica
INK, GREY, WHITE = "#1e1e1e", "#495057", "#ffffff"
PIM_ST = "#1864ab"
REGION = "#868e96"
CPU_ON = GREY                          # awake: solid
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
BLOCKS = [1, 3, 5, 7]                  # PHASE() blocks of gpt3.c: QKV, W_o, W_1, W_2
# host bars: (label, run, total cycles, asleep cycles)
HOSTS = [("MinorCPU", "gpt3_scr64_minor", 2285603, 952950),
         ("O3CPU",    "gpt3_scr64_o3",    1584037, 959983),
         ("O3CPU*",   "gpt3_scr64_o3v61", 1586830, 959683)]
PIMONY_ALONE = 705145                  # RESULTS.md sec. 1, sum row (PIMony as shipped)
PIMONY_OURS = 941105                   # RESULTS.md sec. 3, "PIMony, our work"
PERIOD, TCK = 333, 937                 # CPU / DRAM clock period, ticks
LOGS = os.path.expanduser("~/pimony_env/gem5/configs/scratch/progs/pimony_baseline/logs")

def check_run(run, tot, sl):
    """Recompute from stats.txt blocks 1, 3, 5, 7 when the run is on disk."""
    f = os.path.expanduser(f"~/pimony_env/gem5/m5out/{run}/stats.txt")
    if not os.path.exists(f):
        warnings.append(f"not checked: {f} missing")
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
        warnings.append(f"not checked: {LOGS}/{sub}/{prefix}_*.log missing")
        return
    dram = sum(int(l.split()[-2]) for f in fs for l in open(f) if "GEMV done latency" in l)
    assert round(dram * TCK / PERIOD) == cyc, (sub, dram)

for _, run, tot, sl in HOSTS:
    check_run(run, tot, sl)
check_pimony("standalone", "gpt3", PIMONY_ALONE)
check_pimony("aligned", "seq", PIMONY_OURS)

# ---------------------------------------------------------------- layout
LBL = 18                               # all text; >= 9 pt at 0.85\linewidth (checked below)
TEXTWIDTH_PT = 415                     # \textwidth, lower of the 415-445 pt implied by earlier figures
X0, TOP, PH = 60, 40, 340              # y axis, top of plot (2.5 M), plot height
YMAX = 2.5e6
PXM = PH / YMAX                        # px per cycle
BASE = TOP + PH
BW, PAD, GAP, SEP = 80, 34, 48, 40     # bar width, plot edge to bar, between hosts, bar to separator
XA = X0 + PAD                          # PIMony alone
XS = XA + BW + SEP                     # separator
XH = [XS + SEP + k * (BW + GAP) for k in range(3)]
XR = XH[-1] + BW + PAD                 # right end of the plot
LH = LBL * 1.25

def y_of(c):
    return BASE - c * PXM

def block(x, lo, hi, kind):
    """Fill between lo and hi cycles; no outlines, so every fill ends exactly on its value."""
    y, h = y_of(hi), (hi - lo) * PXM
    if kind == "alone":
        rect(x + 0.5, y + 0.5, BW - 1, h - 1, PIM_ST, PIM_ST, "hachure")   # stroke's outer edge on the value
    else:
        rect(x, y, BW, h, NONE, {"awake": CPU_ON, "asleep": PIM_ST}[kind])

# ---- grid and axis
for k in range(6):
    y = y_of(k * 0.5e6)
    if k:
        line([(X0, y), (XR, y)], GRID)
    line([(X0 - 5, y), (X0, y)], INK)
    text(X0 - 8, y - LH / 2, "0" if k == 0 else f"{k / 2:.1f}", LBL, align="right")
text(X0 - 8 - text_w("2.5", LBL), TOP - LH - 12, "million CPU cycles", LBL)

# ---- bars
def total_label(x, c):
    text(x + BW / 2, y_of(c) - LH - 3, f"{c / 1e6:.2f}", LBL, align="center")

block(XA, 0, PIMONY_ALONE, "alone")
total_label(XA, PIMONY_ALONE)
text(XA + BW / 2, BASE + 8, "PIMony alone", LBL, align="center")
for (name, _, tot, sl), x in zip(HOSTS, XH):
    block(x, 0, sl, "asleep")
    block(x, sl, tot, "awake")
    text(x + BW / 2, (y_of(tot) + y_of(sl)) / 2 - LH / 2 + 1, f"{(tot - sl) / 1e6:.2f}", LBL, WHITE,
         "center", box=(x, x + BW))
    total_label(x, tot)
    text(x + BW / 2, BASE + 8, name, LBL, align="center")
line([(XS, TOP), (XS, BASE + 8 + LH)])                                     # PIMony alone | hosts
line([(X0, TOP), (X0, BASE), (XR, BASE)], INK)
line([(X0, y_of(PIMONY_OURS)), (XR, y_of(PIMONY_OURS))], INK, 1.5, "dashed")

# ---- legend, 2 x 2 below the plot
LY = BASE + 8 + LH + 26
items = [("awake", "CPU awake"), ("asleep", "CPU asleep in pim.wait (PIM busy)"),
         ("alone", "PIM busy, no CPU"), ("dash", "PIMony alone, same jobs")]
cols = [max(text_w(n, LBL) for _, n in items[c::2]) for c in (0, 1)]
lx0 = (X0 - 8 - text_w("2.5", LBL) + XR) / 2 - (38 * 2 + cols[0] + cols[1] + 36) / 2
for i, (kind, name) in enumerate(items):
    lx, ly = lx0 + (i % 2) * (38 + cols[0] + 36), LY + (i // 2) * 30
    if kind == "dash":
        line([(lx, ly + 10), (lx + 30, ly + 10)], INK, 1.5, "dashed")
    elif kind == "alone":
        rect(lx + 0.5, ly + 0.5, 29, 19, PIM_ST, PIM_ST, "hachure")
    else:
        rect(lx, ly, 30, 20, NONE, {"awake": CPU_ON, "asleep": PIM_ST}[kind])
    text(lx + 38, ly - 1, name, LBL)

# ---- text size at 0.85\linewidth (export_pdf.py pads 10 px each side)
x_lo = min(e["x"] for e in els)
x_hi = max(e["x"] + e["width"] for e in els)
pt = LBL * 0.85 * TEXTWIDTH_PT / (x_hi - x_lo + 20)
if pt < 9:
    warnings.append(f"text {pt:.1f} pt at 0.85\\linewidth, below 9 pt")

# ---------------------------------------------------------------- write
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pim_time_split.excalidraw")
json.dump({"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
           "elements": els, "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
           "files": {}}, open(out, "w"), indent=1)
print(f"wrote {out}: {len(els)} elements, {x_hi - x_lo + 20:.0f} px wide, text {pt:.1f} pt at 0.85\\linewidth")
print(f"{'PIMony alone':16s} total {PIMONY_ALONE:>9,}")
for name, run, tot, sl in HOSTS:
    print(f"{name:16s} total {tot:>9,}  asleep {sl:>7,}  awake {tot - sl:>9,}  ({run})")
print(f"{'PIMony, our jobs':16s} dashed {PIMONY_OURS:>8,}")
for w in warnings:
    print("WARN", w)
