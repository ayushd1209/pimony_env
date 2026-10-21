#!/usr/bin/env python3
"""pim.gemv sequencer figure for the thesis (Section: The Memory-Side Sequencer).

Shows only what that section needs. Colour = provenance, as in PIMony's Fig. 5
(red = new circuitry): black = LPDDR5X / standard, blue = PIMony, red = this work.
Traced to code: pimony.cc (job register, descriptor fetch, IRQ), memory_system.cc
(sequencer), PIMSim pim_controller.cc + pim_command_queue.cc (channel controller),
LPDDR5X_12Gb_x16_8533_pimony.ini (geometry).
Run: python3 gen_gemv_sequencer.py  ->  pim_gemv_sequencer.excalidraw
"""
import json, math, itertools, os

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

def text_w(s, size, font):
    lines = s.split("\n")
    if font == MONO:
        return max(len(l) for l in lines) * 0.6 * size
    return max(sum(_HELV.get(c, 1000) for c in l) for l in lines) / 1000 * size

_ids = itertools.count(1)
els, warnings = [], []
_group = []                            # groupIds stamped on new elements

def _base(kind, x, y, w, h, **kw):
    n = next(_ids)
    e = {"id": f"{kind[:2]}{n}", "type": kind, "x": x, "y": y, "width": w, "height": h,
         "angle": 0, "strokeColor": INK, "backgroundColor": "transparent",
         "fillStyle": "solid", "strokeWidth": 1.5, "strokeStyle": "solid",
         "roughness": 0, "opacity": 100, "groupIds": list(_group), "frameId": None,
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

def _poly(kind, pts, st, sw, dash, start, end):
    x0, y0 = pts[0]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return _base(kind, x0, y0, max(xs) - min(xs), max(ys) - min(ys), strokeColor=st,
                 strokeWidth=sw, strokeStyle=dash, points=[[px - x0, py - y0] for px, py in pts],
                 lastCommittedPoint=None, startBinding=None, endBinding=None,
                 startArrowhead=start, endArrowhead=end)

def arrow(pts, st=INK, sw=1.5, both=False):
    return _poly("arrow", pts, st, sw, "solid", "triangle" if both else None, "triangle")

def line(pts, st=INK, sw=1.0):
    return _poly("line", pts, st, sw, "solid", None, None)

def dot(x, y, st):
    ellipse(x - 3.5, y - 3.5, 7, 7, st, st)

def step(cx, cy, n):
    """Step marker sitting on a wire, numbered in job order."""
    ellipse(cx - 10, cy - 10, 20, 20, NEW_ST, NEW_ST)
    ctext(cx, cy, str(n), 13, SANS, WHITE)

def block(x, y, w, h, title, lines=(), kind="std", tsize=18, lsize=15):
    st, bg, sw = {"std": (STD_ST, STD_BG, 1.5), "pim": (PIM_ST, PIM_BG, 1.5),
                  "new": (NEW_ST, WHITE, 2.0)}[kind]
    rect(x, y, w, h, st, bg, sw)
    rows = [(title, tsize, INK)] + [(l, lsize, GREY) for l in lines]
    cy = y + (h - sum(len(s.split("\n")) * z * 1.25 for s, z, _ in rows)) / 2
    for s, size, col in rows:
        text(x + w / 2, cy, s, size, SANS, col, "center", box=(x + 4, x + w - 4))
        cy += len(s.split("\n")) * size * 1.25

def stack(x, y, w, h, n=2, d=6):
    """Shadow copies behind a box: one copy of the box per channel."""
    for i in range(n, 0, -1):
        rect(x + i * d, y - i * d, w, h)

def group(name):
    _group[:] = [name] if name else []

# ---------------------------------------------------------------- layout
L, R = 50, 840                               # content left / right edge
J = (60, 220)                                # job register / descriptor fetch
G = (255, 545)                               # command generator
P = (300, 440)                               # issue port
T = (550, 830)                               # engine table
C = (590, 820)                               # completion
GEMV_X, WAIT_X = 140, 705                    # where the two instructions cross

# ---- core
group("core")
CORE_B = 44
rect(L, 0, R - L, CORE_B)
ctext((GEMV_X + 55 + WAIT_X - 55) / 2, 22, "RISC-V core", 18)
for cx, mnem in ((GEMV_X, "pim.gemv"), (WAIT_X, "pim.wait")):
    rect(cx - 55, 8, 110, 28, NEW_ST, NEW_BG, 2.0)
    ctext(cx, 22, mnem, 16, MONO)

# ---- memory controller region
group(None)
MC_T, MC_B = 84, 578
rect(34, MC_T, R + 16 - 34, MC_B - MC_T, REGION, "transparent", 1.5, "dashed")
mc = ctext(20, (MC_T + MC_B) / 2, "Memory controller", 17, SANS, GREY)
mc["angle"] = 3 * math.pi / 2                # reads bottom-to-top

# ---- sequencer (this work)
group("sequencer")
SQ_T, SQ_B = 96, 402
rect(L, SQ_T, R - L, SQ_B - SQ_T, NEW_ST, NEW_BG, 2.5)
ctext((L + R) / 2, SQ_T + 17, "pim.gemv sequencer", 19, SANS, NEW_ST)

A_T, A_B = 136, 212                          # control row
B_T, B_B = 272, 392                          # datapath row
FETCH_B = 348
block(J[0], A_T, J[1] - J[0], A_B - A_T, "Job register", ["one job at a time"], "new")
block(J[0], 290, J[1] - J[0], FETCH_B - 290, "Descriptor fetch", kind="new")
block(G[0], A_T, G[1] - G[0], A_B - A_T, "Command generator", kind="new")
block(C[0], A_T, C[1] - C[0], A_B - A_T, "Completion", ["reported once"], "new")
block(P[0], 290, P[1] - P[0], B_B - 290, "Issue port", kind="new")

# engine table: one row per engine
rect(T[0], B_T, T[1] - T[0], B_B - B_T, NEW_ST, WHITE, 2.0)
text((T[0] + T[1]) / 2, B_T + 5, "Engine table", 16, SANS, INK, "center",
     box=(T[0] + 4, T[1] - 4))
cols = [("engine", 54), ("busy", 54), ("readout owed", 96), ("write owed", 76)]
gy, hdr, rh = B_T + 28, 22, 17.5
xs = [T[0]]
for _, w in cols:
    xs.append(xs[-1] + w)
assert abs(xs[-1] - T[1]) < 1, xs
for yy, sw in ((gy, 1.0), (gy + hdr, 1.0)):
    line([(T[0], yy), (T[1], yy)], NEW_ST, sw)
for i in range(1, 4):
    line([(T[0], gy + hdr + i * rh), (T[1], gy + hdr + i * rh)], NEW_ST, 0.6)
for xx in xs[1:-1]:
    line([(xx, gy), (xx, B_B)], NEW_ST, 0.6)
for (name, _), a, b in zip(cols, xs, xs[1:]):
    ctext((a + b) / 2, gy + hdr / 2, name, 14, SANS, INK, box=(a + 2, b - 2))
for i, idx in enumerate(["0", "1", "⋮", "31"]):
    cy = gy + hdr + rh * i + rh / 2
    ctext((xs[0] + xs[1]) / 2, cy, idx, 13, SANS, GREY)
    if idx == "⋮":
        for a, b in zip(xs[1:], xs[2:]):
            ctext((a + b) / 2, cy, idx, 13, SANS, GREY)

# internal wiring
arrow([(J[1], 174), (G[0], 174)], NEW_ST)                                 # job -> generator
arrow([(GEMV_X, A_B), (GEMV_X, 290)], NEW_ST, both=True)                  # register <-> fetch
arrow([(G[1], 174), (C[0], 174)], NEW_ST)                                 # all issued
arrow([(WAIT_X, B_T), (WAIT_X, A_B)], NEW_ST)                             # nothing owed

D2G_X, MAC_X = 330, 410                                                   # generator -> port
arrow([(D2G_X, A_B), (D2G_X, 290)], NEW_ST)
step(D2G_X, 231, 2)
pc = text(D2G_X - 16, 222, "(per channel)", 14, SANS, GREY, "right")      # one per channel
text(pc["x"] - 4, 222, "D2GWRITE", 14, MONO, INK, "right", box=(GEMV_X + 12, pc["x"]))
line([(MAC_X, A_B), (MAC_X, 250)], NEW_ST, 1.5)
rect(MAC_X - 21, 250, 42, 20, NEW_ST, WHITE, 1.5)                         # busy gate
ctext(MAC_X, 260, "free?", 13)
arrow([(MAC_X, 270), (MAC_X, 290)], NEW_ST)
step(MAC_X, 231, 3)
text(MAC_X + 16, 222, "MAC", 14, MONO)
BUSY_X = (xs[1] + xs[2]) / 2                                              # busy column
arrow([(BUSY_X, B_T), (BUSY_X, 260), (MAC_X + 21, 260)], NEW_ST)          # busy -> gate
text(530, 243, "busy", 14, SANS, GREY, "right")

for yy, n, mnem in ((332, 4, "READRES"), (370, 5, "WRITE")):             # owed work -> port
    arrow([(T[0], yy), (P[1], yy)], NEW_ST)
    step(P[1] + 28, yy, n)
    text(P[1] + 42, yy - 19, mnem, 14, MONO, box=(P[1], T[0]))

# ---- channel controller (one per channel)
# PIM requests share the read queue (pim_controller.cc:369); PIMony's addition
# is the PIM command queue (pim_command_queue.cc GetQueue)
group("channel")
CC_T, CC_B = 462, 564
stack(L, CC_T, R - L, CC_B - CC_T)
rect(L, CC_T, R - L, CC_B - CC_T)
ctext(560, CC_T + 15, "Channel controller (×4 channels)", 17)
Q_T, Q_B = 490, 552
CA_X = 631                                                                # C/A leaves here
block(70, Q_T, 218, Q_B - Q_T, "Read queue", ["reads + PIM requests"], tsize=15, lsize=13)
block(300, Q_T, 140, Q_B - Q_T, "Write queue", tsize=15)
block(455, Q_T, 90, Q_B - Q_T, "Scheduler", tsize=15)
rect(557, Q_T, 188, Q_B - Q_T)
text(651, Q_T + 3, "Command queues", 14, SANS, INK, "center", box=(561, 741))
block(565, Q_T + 22, CA_X - 4 - 565, Q_B - 4 - Q_T - 22, "per bank", tsize=13)
block(CA_X + 4, Q_T + 22, 737 - CA_X - 4, Q_B - 4 - Q_T - 22, "PIM queue", kind="pim", tsize=13)
block(755, Q_T, 75, Q_B - Q_T, "Return\nqueue", tsize=15)

# ---- sequencer <-> channel controller <-> core
group(None)
arrow([(GEMV_X, FETCH_B), (GEMV_X, Q_T)], NEW_ST, both=True)             # descriptor read
step(GEMV_X, 419, 1)
text(GEMV_X + 16, 410, "descriptor read", 14, SANS, GREY)
OUT_X = (P[0] + P[1]) / 2                                                 # commands down
arrow([(OUT_X, B_B), (OUT_X, Q_T)], NEW_ST)                               # WRITE -> write queue
arrow([(OUT_X, 436), (250, 436), (250, Q_T)], NEW_ST)                     # PIM -> read queue
dot(OUT_X, 436, NEW_ST)
RESP_X = 792                                                              # return queue centre
arrow([(RESP_X, Q_T), (RESP_X, B_B)], NEW_ST)                             # responses up
text(RESP_X - 10, 410, "responses", 14, SANS, GREY, "right")
arrow([(GEMV_X, 36), (GEMV_X, A_T)], NEW_ST)                              # pim.gemv in
text(GEMV_X + 10, 57, "descriptor address, shape", 14, SANS, GREY)
arrow([(WAIT_X, A_T), (WAIT_X, CORE_B)], NEW_ST)                          # IRQ to the core
step(WAIT_X, 68, 6)
text(WAIT_X + 16, 59, "completion IRQ", 14, SANS, GREY)

# ---- LPDDR5X-PIM device (one per channel)
group("device")
DV_T, DV_B = 618, 744
stack(L, DV_T, R - L, DV_B - DV_T)
rect(L, DV_T, R - L, DV_B - DV_T)
ctext(284, DV_T + 15, "LPDDR5X-PIM device (×4 channels)", 17)
BG_T = DV_T + 34
for i in range(4):                                                        # one rank shown
    bx = 66 + i * 86
    rect(bx, BG_T, 80, 66)
    ctext(bx + 40, BG_T + 11, f"BG{i}", 13)
    for k in range(4):
        rect(bx + 6 + k * 17.5, BG_T + 22, 15, 22, STD_ST, WHITE, 1.0)
        rect(bx + 6 + k * 17.5, BG_T + 47, 15, 11, PIM_ST, PIM_BG, 1.0)
text(66, DV_B - 21, "engine = one bankgroup: 4 banks + 4 PUs (32 in total)", 13, SANS, GREY,
     box=(66, 510))
block(418, BG_T, 84, 66, "Global\nbuffer", kind="pim", tsize=15)
rect(518, BG_T, 306, 84)
text(724, BG_T + 6, "Control logic", 15, SANS, INK, "center")
block(530, BG_T + 30, CA_X - 5 - 530, 44, "CMD decoder", tsize=13)
block(CA_X + 5, BG_T + 30, 812 - CA_X - 5, 44, "MAC control", kind="pim", tsize=13)
group(None)
arrow([(CA_X, Q_B), (CA_X, BG_T)], INK, both=True)                        # C/A + DQ
text(CA_X + 10, 584, "C/A, DQ", 14, SANS, GREY)

# ---- legend
group("legend")
LY = DV_B + 24
items = [(STD_ST, STD_BG, 1.5, "LPDDR5X / standard"), (PIM_ST, PIM_BG, 1.5, "PIMony"),
         (NEW_ST, NEW_BG, 2.0, "This work")]
total = sum(34 + text_w(n, 15, SANS) for *_, n in items) + 36 * (len(items) - 1)
lx = (L + R) / 2 - total / 2
for st, bg, sw, name in items:
    rect(lx, LY, 26, 16, st, bg, sw)
    t = text(lx + 34, LY - 1, name, 15)
    lx += 34 + t["width"] + 36
group(None)

# ---------------------------------------------------------------- write
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pim_gemv_sequencer.excalidraw")
json.dump({"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
           "elements": els, "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
           "files": {}}, open(out, "w"), indent=1)
print(f"wrote {out}: {len(els)} elements")
for w in warnings:
    print("WARN", w)
