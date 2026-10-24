#!/usr/bin/env python3
"""One GPT-3 decoder layer to scale, all heads on CPU vs all heads on PIM (thesis 6.6, sec:eval:overlap).

Two rows on one time axis, two lanes each. CPU: awake solid, asleep in pim.wait hatched,
summed per section (awake first). PIM: busy while the CPU sleeps, idle elsewhere.
Sections = the eight PHASE() blocks of gpt3.c, named as its comments name them.
Data: m5out/gpt3_hs2_n0_o3 and gpt3_hs2_n32_o3; cycles = simTicks / 333 (clock period 333 ps),
asleep = cycles - numCycles (= quiesceCycles). Checked against stats.txt when it is there.
Run: python3 gen_layer_gantt.py  ->  layer_gantt.excalidraw
"""
import json, itertools, os

SANS = 2                               # Excalidraw: Helvetica
INK, GREY, WHITE = "#1e1e1e", "#495057", "#ffffff"
PIM_ST = "#1864ab"
REGION = "#868e96"
CPU_ON = GREY                          # awake: solid
SLEEP_ST, SLEEP_HATCH = REGION, "#adb5bd"
IDLE_BG = "#f8f9fa"

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

def line(pts, st=REGION, sw=1):
    x0, y0 = pts[0]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return _base("line", x0, y0, max(xs) - min(xs), max(ys) - min(ys), strokeColor=st,
                 strokeWidth=sw, points=[[px - x0, py - y0] for px, py in pts],
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
SECTIONS = ["LN 1 + QKV", "attention", "Wo", "residual 1 + LN 2", "W1", "GELU", "W2", "residual 2"]
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
        warnings.append(f"not checked: {f} missing")
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
X0, PX = 52, 830 / 3.6e6               # time 0, px per cycle
LANE_H, LANE_GAP = 22, 5
T1, T2 = 20, 40                        # section labels: tier 1 (fits) / tier 2 (sliver, with leader) above the CPU lane
ROW_TOP = [0, 150]

def bar(x, w, y, st, bg, fill="solid", dash="solid"):
    if w < 2 and fill == "solid":
        st = bg                        # a white edge would hide a sliver
    rect(x, y, w, LANE_H, st, bg, fill, dash)

for (title, _, rows), top in zip(RUNS, ROW_TOP):
    text(X0, top, title, 16)
    cpu_y = top + 22 + T2
    pim_y = cpu_y + LANE_H + LANE_GAP
    text(X0 - 8, cpu_y + 3, "CPU", 14, align="right")
    text(X0 - 8, pim_y + 3, "PIM", 14, align="right")
    c = 0
    for name, (tot, sl) in zip(SECTIONS, rows):
        x, w = X0 + c * PX, tot * PX
        aw, sw = (tot - sl) * PX, sl * PX
        bar(x, aw, cpu_y, WHITE, CPU_ON)                                   # CPU awake
        if sl:
            bar(x + aw, sw, cpu_y, SLEEP_ST, SLEEP_HATCH, "hachure")      # CPU asleep
        rect(x, pim_y, aw, LANE_H, REGION, IDLE_BG, dash="dashed")  # PIM idle
        if sl:
            bar(x + aw, sw, pim_y, WHITE, PIM_ST)                          # PIM busy
        if name == "attention" and aw > 200:
            text(x + aw / 2, pim_y + 3, "idle", 14, GREY, "center")
        line([(x, cpu_y - 5), (x, cpu_y)])                                 # section boundary
        cx, tw = x + w / 2, text_w(name, 14)
        if tw <= w - 4:
            text(cx, cpu_y - T1, name, 14, align="center", box=(x, x + w))
        else:
            text(cx, cpu_y - T2, name, 14, align="center")
            line([(cx, cpu_y - T2 + 18), (cx, cpu_y)])
        c += tot
    line([(X0 + c * PX, cpu_y - 5), (X0 + c * PX, cpu_y)])

# ---- time axis
AY = ROW_TOP[1] + 22 + T2 + 2 * LANE_H + LANE_GAP + 14
line([(X0, AY), (X0 + 3.6e6 * PX, AY)], INK)
for k in range(8):
    x = X0 + k * 0.5e6 * PX
    line([(x, AY), (x, AY + 5)], INK)
    text(x, AY + 7, "0" if k == 0 else f"{k / 2:.1f}", 13, align="center")
text(X0 + 1.8e6 * PX, AY + 26, "time (million CPU cycles)", 14, align="center")

# ---- legend
LY = AY + 58
items = [(WHITE, CPU_ON, "solid", "solid", "CPU awake"),
         (SLEEP_ST, SLEEP_HATCH, "hachure", "solid", "CPU asleep in pim.wait"),
         (WHITE, PIM_ST, "solid", "solid", "PIM busy"),
         (REGION, IDLE_BG, "solid", "dashed", "PIM idle")]
total = sum(34 + text_w(n, 14) for *_, n in items) + 30 * (len(items) - 1)
lx = X0 + 1.8e6 * PX - total / 2
for st, bg, fill, dash, name in items:
    rect(lx, LY, 26, 16, REGION if st == WHITE else st, bg, fill, dash)
    t = text(lx + 34, LY - 1, name, 14)
    lx += 34 + t["width"] + 30

# ---------------------------------------------------------------- write
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "layer_gantt.excalidraw")
json.dump({"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
           "elements": els, "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
           "files": {}}, open(out, "w"), indent=1)
print(f"wrote {out}: {len(els)} elements")
for w in warnings:
    print("WARN", w)
