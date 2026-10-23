#!/usr/bin/env python3
"""One decode step of an attention head on the PIM, for the thesis (Section 6.6, sec:eval:overlap).

Two lanes, CPU left and PIM right, time running down; step numbers are the five steps
of the text. Colour = provenance, as in gen_gemv_sequencer.py: black = standard,
blue = PIMony, red = this work (our instructions).
Traced to code: gpt3.c -DPIM_ATT, pim_att_append() (step 1) and pim_att_head() with
pim_att_stage() and pim_att_issue() (steps 2-5); every clean and invalidate is drawn.
"done" = pimComplete() in pimony.cc raising the completion interrupt that ends pim.wait.
Run: python3 gen_pim_head.py  ->  pim_head_step.excalidraw
"""
import json, itertools, os

SANS, MONO = 2, 3                      # Excalidraw: Helvetica, Cascadia
INK, GREY, WHITE = "#1e1e1e", "#495057", "#ffffff"
STD_ST, STD_BG = "#1e1e1e", "#ffffff"
PIM_ST, PIM_BG = "#1864ab", "#e7f5ff"
NEW_ST, NEW_BG = "#c92a2a", "#fff5f5"
REGION = "#868e96"

# Helvetica advance widths (AFM, per 1000 em) for the overflow check
_HELV = dict(zip(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~",
    [278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278]+[556]*10+
    [278,278,584,584,584,556,1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,667,778,722,667,611,
     722,667,944,667,667,611,278,278,278,469,556,333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,
     556,556,333,500,278,556,500,722,500,500,500,334,260,334,584]))
_HELV.update({"×": 584, "ᵀ": 433})    # from arial.ttf, which draws Helvetica on Windows

def text_w(s, size, font):
    lines = s.split("\n")
    if font == MONO:
        return max(len(l) for l in lines) * 0.6 * size
    return max(sum(_HELV.get(c, 1000) for c in l) for l in lines) / 1000 * size

_ids = itertools.count(1)
els, warnings = [], []

def _base(kind, x, y, w, h, **kw):
    n = next(_ids)
    e = {"id": f"{kind[:2]}{n}", "type": kind, "x": x, "y": y, "width": w, "height": h,
         "angle": 0, "strokeColor": INK, "backgroundColor": "transparent",
         "fillStyle": "solid", "strokeWidth": 1.5, "strokeStyle": "solid",
         "roughness": 0, "opacity": 100, "groupIds": [], "frameId": None,
         "roundness": None, "seed": n * 7919 % 2147483647, "version": 1,
         "versionNonce": n * 104729 % 2147483647, "isDeleted": False,
         "boundElements": [], "updated": 1, "link": None, "locked": False}
    e.update(kw)
    els.append(e)
    return e

def rect(x, y, w, h, st=STD_ST, bg=STD_BG, sw=1.5, dash="solid"):
    return _base("rectangle", x, y, w, h, strokeColor=st, backgroundColor=bg,
                 strokeWidth=sw, strokeStyle=dash)

def ellipse(x, y, w, h, st, bg):
    return _base("ellipse", x, y, w, h, strokeColor=st, backgroundColor=bg, strokeWidth=1)

def text(x, y, s, size=15, font=SANS, color=INK, align="left", box=None):
    """x is the left edge (align=left), the centre (center) or the right edge (right)."""
    w = text_w(s, size, font)
    h = size * 1.25 * len(s.split("\n"))
    left = {"left": x, "center": x - w / 2, "right": x - w}[align]
    if box and (left < box[0] - 0.5 or left + w > box[1] + 0.5):
        warnings.append(f"overflow: {s!r} [{left:.0f}..{left + w:.0f}] not in [{box[0]}..{box[1]}]")
    return _base("text", left, y, w, h, strokeColor=color, fontSize=size, fontFamily=font,
                 text=s, originalText=s, textAlign=align, verticalAlign="top",
                 containerId=None, lineHeight=1.25, baseline=round(size * 0.9), autoResize=True)

def ctext(cx, cy, s, size=15, font=SANS, color=INK, box=None):
    """Text centred on (cx, cy)."""
    h = size * 1.25 * len(s.split("\n"))
    return text(cx, cy - h / 2, s, size, font, color, "center", box)

def arrow(pts, st=INK, sw=1.5):
    x0, y0 = pts[0]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return _base("arrow", x0, y0, max(xs) - min(xs), max(ys) - min(ys), strokeColor=st,
                 strokeWidth=sw, points=[[px - x0, py - y0] for px, py in pts],
                 lastCommittedPoint=None, startBinding=None, endBinding=None,
                 startArrowhead=None, endArrowhead="triangle")

def step(cx, cy, n):
    """Step marker: the text's step number. Neutral, a step is not provenance."""
    ellipse(cx - 11, cy - 11, 22, 22, INK, INK)
    ctext(cx, cy, str(n), 14, SANS, WHITE)

def entry(x, y, w, h, s, kind, cy):
    """One lane entry, label on row centre cy; our instructions are mono, as in the sequencer figure."""
    st, bg, sw, font, size = {"std": (STD_ST, STD_BG, 1.5, SANS, 16), "pim": (PIM_ST, PIM_BG, 1.5, SANS, 16),
                              "new": (NEW_ST, NEW_BG, 2.0, MONO, 15)}[kind]   # mono 15 = x-height of sans 16
    rect(x, y, w, h, st, bg, sw)
    ctext(x + w / 2, cy, s, size, font, box=(x + 4, x + w - 4))

# ---------------------------------------------------------------- the step, in code order
# (step, CPU entry, PIM entry). "pim." = one of our instructions -> red.
# job = PIM busy for this row and the next, done wakes that pim.wait; data = result, read by the next row.
ROWS = [
    (1, "store new k, v",         None),                       # pim_att_append()
    (0, "pim.fence (clean)",      None),
    (2, "store q",                None),                       # pim_att_stage(q)
    (0, "pim.fence (clean)",      None),
    (0, "store descriptor",       None),                       # pim_att_issue(0)
    (0, "pim.fence (clean)",      None),
    (0, "pim.gemv",               ("job", "K × q")),
    (0, "pim.wait (sleeps)",      None),
    (0, "pim.fence (invalidate)", ("data", "scores in DRAM")),  # pim_inv_range(attout)
    (0, "load scores",            None),
    (3, "softmax",                None),
    (4, "store weights",          None),                       # pim_att_stage(weights)
    (0, "pim.fence (clean)",      None),
    (5, "store descriptor",       None),                       # pim_att_issue(1)
    (0, "pim.fence (clean)",      None),
    (0, "pim.gemv",               ("job", "Vᵀ × weights")),
    (0, "pim.wait (sleeps)",      None),
    (0, "pim.fence (invalidate)", ("data", "output in DRAM")),
    (0, "load output",            None),
]

# ---------------------------------------------------------------- layout
SX = 15                                # step markers
CL, CR = 46, 278                       # CPU entries
PL, PR = 382, 526                      # PIM entries
PAD = 12                               # lane edge around its entries
TOP, PITCH, BH, GAP = 40, 29, 24, 12   # first row, row pitch, entry height, extra space between steps

ys, y = [], TOP
for i, (n, _, _) in enumerate(ROWS):
    if n and i:
        y += GAP
    ys.append(y)
    y += PITCH
LANE_B = ys[-1] + BH + PAD

# ---- lanes
for l, r, name in ((CL, CR, "CPU"), (PL, PR, "PIM")):
    rect(l - PAD, 0, r - l + 2 * PAD, LANE_B, REGION, "transparent", 1.5, "dashed")
    ctext((l + r) / 2, TOP / 2, name, 18)

# ---- entries
for i, ((n, s, pim), y) in enumerate(zip(ROWS, ys)):
    cy = y + BH / 2
    entry(CL, y, CR - CL, BH, s, "new" if s.startswith("pim.") else "std", cy)
    if n:
        step(SX, cy, n)
    if not pim:
        continue
    kind, label = pim
    nxt, ny = ROWS[i + 1][1], ys[i + 1] + BH / 2           # the CPU row this entry ends at
    if kind == "job":
        assert nxt.startswith("pim.wait"), "a job ends at its pim.wait"
        entry(PL, y, PR - PL, PITCH + BH, label, "pim", y + (PITCH + BH) / 2)
        arrow([(CR, cy), (PL, cy)])                          # pim.gemv starts the job
        arrow([(PL, ny), (CR, ny)])                          # completion wakes pim.wait
        ctext((CR + PL) / 2, ny - 9, "done", 15, SANS, GREY)
    else:
        assert nxt.startswith("load"), "the result is read by a load"
        entry(PL, y, PR - PL, BH, label, "std", cy)
        arrow([(PL, cy), (CR, ny)])

# ---- legend
LY = LANE_B + 22
items = [(STD_ST, STD_BG, 1.5, "Standard"), (PIM_ST, PIM_BG, 1.5, "PIMony"),
         (NEW_ST, NEW_BG, 2.0, "This work")]
total = sum(34 + text_w(n, 15, SANS) for *_, n in items) + 36 * (len(items) - 1)
lx = (CL - PAD + PR + PAD) / 2 - total / 2
for st, bg, sw, name in items:
    rect(lx, LY, 26, 16, st, bg, sw)
    t = text(lx + 34, LY - 1, name, 15)
    lx += 34 + t["width"] + 36

# ---------------------------------------------------------------- write
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pim_head_step.excalidraw")
json.dump({"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
           "elements": els, "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
           "files": {}}, open(out, "w"), indent=1)
print(f"wrote {out}: {len(els)} elements")
for w in warnings:
    print("WARN", w)
