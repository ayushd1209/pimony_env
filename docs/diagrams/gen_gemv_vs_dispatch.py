#!/usr/bin/env python3
"""GPT-3 decode matmul time: pim.gemv vs pim.dispatch (sec:eval:granularity).

Based on gen_pim_time_split.py, with the same Excalidraw helpers and styling.
Matmul blocks 1, 3, 5, 7 (QKV, W_o, W_1, W_2), CPU asleep then CPU awake.
Cycles = simTicks / 333; asleep = cycles - numCycles (= quiesceCycles).
PIMony's same-jobs reference applies only to pim.gemv: dispatch omits buffer
fill, readout and write-back. Checked against stats.txt and aligned PIMony logs.
Horizontal stacked bars, sized for \\linewidth with text at least 9 pt.
Run: python3 gen_gemv_vs_dispatch.py  ->  gemv_vs_dispatch.excalidraw
Then: python3 export_pdf.py gemv_vs_dispatch.excalidraw
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
        warnings.append(f"not checked: {f} missing")
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
        warnings.append(f"not checked: {LOGS}/{sub}/{prefix}_*.log missing")
        return
    dram = sum(int(l.split()[-2]) for f in fs for l in open(f) if "GEMV done latency" in l)
    assert round(dram * TCK / PERIOD) == cyc, (sub, dram)

for _, run, tot, sl in RUNS:
    check_run(run, tot, sl)
check_pimony("aligned", "seq", PIMONY_OURS)

# ---------------------------------------------------------------- layout
LBL = 18                               # same label size as gen_pim_time_split.py
TEXTWIDTH_PT = 415                     # thesis \linewidth, conservative text width
X0, XR = 220, 760                      # plot spans 0 to 3.0 M cycles
TOP, BH, GAP = 42, 36, 28             # bar top, height, gap between bars
YB = [TOP, TOP + BH + GAP]
BASE = YB[-1] + BH + 12                # bottom x axis
XMAX = 3.0e6
PXM = (XR - X0) / XMAX                 # px per CPU cycle
LH = LBL * 1.25

def x_of(c):
    return X0 + c * PXM

def block(y, lo, hi, kind):
    """Fill lo..hi cycles exactly, label inside if it fits, otherwise just above."""
    x, w = x_of(lo), (hi - lo) * PXM
    rect(x, y, w, BH, NONE, {"awake": CPU_ON, "asleep": PIM_ST}[kind])
    label = f"{(hi - lo) / 1e6:.2f}"
    inside = text_w(label, LBL) + 12 <= w and LH + 4 <= BH
    text(x + w / 2, y + (BH - LH) / 2 if inside else y - LH - 4,
         label, LBL, WHITE if inside else INK, "center",
         box=(x, x + w) if inside else None)

# ---- grid and axis
for k in range(7):
    x = x_of(k * 0.5e6)
    if k:
        line([(x, TOP), (x, BASE)], GRID)
    line([(x, BASE), (x, BASE + 5)], INK)
    text(x, BASE + 8, "0" if k == 0 else f"{k / 2:.1f}", LBL, align="center")
text((X0 + XR) / 2, BASE + 8 + LH + 4, "million CPU cycles", LBL, align="center")

# ---- bars and annotations
for (name, _, tot, sl), y in zip(RUNS, YB):
    assert 0 <= sl <= tot <= XMAX, (name, tot, sl)
    block(y, 0, sl, "asleep")
    block(y, sl, tot, "awake")
    label = name + ("\n32 commands, then wait" if name == "pim.dispatch" else "")
    label_h = LH * len(label.split("\n"))
    text(X0 - 16, y + (BH - label_h) / 2, label, LBL, align="right")
    text(x_of(tot) + 8, y + (BH - LH) / 2, f"{tot / 1e6:.2f}", LBL)
line([(X0, TOP), (X0, BASE), (XR, BASE)], INK)
# Same jobs only for pim.gemv; dispatch runs do less PIM work.
REF_TOP, REF_BOTTOM = YB[0] - 10, YB[0] + BH + 10
assert REF_BOTTOM < YB[1]
line([(x_of(PIMONY_OURS), REF_TOP), (x_of(PIMONY_OURS), REF_BOTTOM)], INK, 1.5, "dashed")
text(x_of(PIMONY_OURS), REF_TOP - LH - 3, f"{PIMONY_OURS / 1e6:.2f}", LBL, align="center")

# ---- legend: same swatches and dashed sample, two columns below the axis
LY = BASE + 8 + 2 * LH + 4 + 14
items = [("asleep", "CPU asleep in pim.wait (PIM busy)"), ("awake", "CPU awake"),
         ("dash", "PIMony alone, same jobs")]
cols = [max(text_w(n, LBL) for _, n in items[c::2]) for c in (0, 1)]
figure_left = X0 - 16 - text_w("32 commands, then wait", LBL)
figure_right = max(XR + text_w("3.0", LBL) / 2,
                   max(x_of(tot) + 8 + text_w(f"{tot / 1e6:.2f}", LBL) for _, _, tot, _ in RUNS))
lx0 = (figure_left + figure_right) / 2 - (38 * 2 + sum(cols) + 36) / 2
for i, (kind, name) in enumerate(items):
    lx, ly = lx0 + (i % 2) * (38 + cols[0] + 36), LY + (i // 2) * 30
    if kind == "dash":
        line([(lx, ly + 10), (lx + 30, ly + 10)], INK, 1.5, "dashed")
    else:
        rect(lx, ly, 30, 20, NONE, {"awake": CPU_ON, "asleep": PIM_ST}[kind])
    text(lx + 38, ly - 1, name, LBL)

# ---- text size at \linewidth (export_pdf.py pads 10 px each side)
x_lo = min(e["x"] for e in els)
x_hi = max(e["x"] + e["width"] for e in els)
texts = [e for e in els if e["type"] == "text"]
pt = min(e["fontSize"] for e in texts) * TEXTWIDTH_PT / (x_hi - x_lo + 20)
if pt < 9:
    warnings.append(f"text {pt:.1f} pt at \\linewidth, below 9 pt")

# Warn for every intersecting pair of text boxes, including annotations/legend.
for a, b in itertools.combinations(texts, 2):
    overlap_w = min(a["x"] + a["width"], b["x"] + b["width"]) - max(a["x"], b["x"])
    overlap_h = min(a["y"] + a["height"], b["y"] + b["height"]) - max(a["y"], b["y"])
    if overlap_w > 0 and overlap_h > 0:
        warnings.append(f"overlapping text: {a['text']!r} and {b['text']!r} ({overlap_w:.1f} x {overlap_h:.1f} px)")

# ---------------------------------------------------------------- write
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gemv_vs_dispatch.excalidraw")
json.dump({"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
           "elements": els, "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
           "files": {}}, open(out, "w"), indent=1)
print(f"wrote {out}: {len(els)} elements, {x_hi - x_lo + 20:.0f} px wide, text {pt:.1f} pt at \\linewidth")
for name, run, tot, sl in RUNS:
    print(f"{name:16s} total {tot:>10,}  asleep {sl:>10,}  awake {tot - sl:>10,}  ({run})")
print(f"{'PIMony, our jobs':16s} dashed {PIMONY_OURS:>9,}  (pim.gemv only)")
for w in warnings:
    print("WARN", w)
