#!/usr/bin/env python3
"""Why per-MAC completion is the bottleneck — one picture, five panels.

The whole argument with no arithmetic left for the reader:
  A. what ONE pim.dispatch covers          (8 KB, 290 cycles)
  B. how many dispatches ONE GEMV needs    (4 MB / 8 KB = 512)
  C. TODAY: every dispatch fires its own interrupt   -> 697 us of handler
  D. GROUPED: one interrupt per engine group          -> 22 us of handler
  E. bars drawn to scale, so the ratio is visual not numeric

All figures traced to config, not assumed:
  LPDDR5X_12Gb_x16_8533_pimony.ini : columns=1024 device_width=16 BL=16
                                     bankgroups=4 banks_per_group=4 channels=4
  configs/model_configs/GEMV.json  : d_model=4096 d_interm=512 FP16
  measured (docs/perf_analysis_notes.md) : MAC=290 cyc, trap handler=1361 cyc
"""
import json, itertools, os

_seed = itertools.count(1000)
def nz(): return next(_seed) * 7919 % 2147483647
elements = []

def shape(kind, x, y, w, h, stroke, bg, sw=1, dash="solid", rounded=True):
    elements.append({
        "type": kind, "id": f"{kind[0]}{nz()}",
        "x": x, "y": y, "width": w, "height": h, "angle": 0,
        "strokeColor": stroke, "backgroundColor": bg, "fillStyle": "solid",
        "strokeWidth": sw, "strokeStyle": dash, "roughness": 1, "opacity": 100,
        "groupIds": [], "frameId": None,
        "roundness": {"type": 3} if (rounded and kind == "rectangle") else None,
        "seed": nz(), "version": 1, "versionNonce": nz(), "isDeleted": False,
        "boundElements": [], "updated": 1, "link": None, "locked": False,
    })

def rect(x, y, w, h, stroke, bg="transparent", sw=1, dash="solid", rounded=True):
    shape("rectangle", x, y, w, h, stroke, bg, sw, dash, rounded)

def text(x, y, s, size=14, color="#1e1e1e", font=1, align="left"):
    lines = s.split("\n")
    elements.append({
        "type": "text", "id": f"t{nz()}", "x": x, "y": y,
        "width": max((len(l) for l in lines), default=1) * size * 0.6,
        "height": round(size * 1.25 * len(lines)), "angle": 0,
        "strokeColor": color, "backgroundColor": "transparent",
        "fillStyle": "solid", "strokeWidth": 1, "strokeStyle": "solid",
        "roughness": 1, "opacity": 100, "groupIds": [], "frameId": None,
        "roundness": None, "seed": nz(), "version": 1, "versionNonce": nz(),
        "isDeleted": False, "boundElements": [], "updated": 1, "link": None,
        "locked": False, "fontSize": size, "fontFamily": font, "text": s,
        "textAlign": align, "verticalAlign": "top", "containerId": None,
        "originalText": s, "lineHeight": 1.25, "baseline": size,
    })

def arrow(pts, stroke, sw=2, dash="solid"):
    x0, y0 = pts[0]; xs=[p[0] for p in pts]; ys=[p[1] for p in pts]
    elements.append({
        "type": "arrow", "id": f"a{nz()}", "x": x0, "y": y0,
        "width": max(xs)-min(xs), "height": max(ys)-min(ys), "angle": 0,
        "strokeColor": stroke, "backgroundColor": "transparent",
        "fillStyle": "solid", "strokeWidth": sw, "strokeStyle": dash,
        "roughness": 1, "opacity": 100, "groupIds": [], "frameId": None,
        "roundness": {"type": 2}, "seed": nz(), "version": 1,
        "versionNonce": nz(), "isDeleted": False, "boundElements": [],
        "updated": 1, "link": None, "locked": False,
        "points": [[p[0]-x0, p[1]-y0] for p in pts], "lastCommittedPoint": None,
        "startBinding": None, "endBinding": None,
        "startArrowhead": None, "endArrowhead": "triangle",
    })

# palette — MAC/compute = teal (good), handler/overhead = red (waste)
MAC="#0ca678"; MACB="#c3fae8"
RED="#e03131"; REDB="#ffe3e3"; REDL="#fff5f5"
GRN="#2f9e44"; GRNB="#d3f9d8"; GRNL="#ebfbee"
BLU="#1971c2"; BLUB="#d0ebff"; BLUL="#e7f5ff"
GREY="#495057"; MUT="#868e96"; INK="#1e1e1e"

L = 60          # left margin
W = 1460        # content width

# ============================ TITLE ============================
text(L, 20, "Per-MAC completion is the bottleneck", 30, INK, 1)
text(L, 60, "one GEMV, three ways of telling the CPU it finished", 16, MUT, 3)

# ==================== PANEL A: one dispatch ====================
y = 110
text(L, y, "A.   what ONE pim.dispatch covers", 18, BLU, 1)
rect(L, y+30, 700, 150, BLU, BLUL, 1)

# the open DRAM row
rect(L+24, y+56, 460, 34, BLU, BLUB, 1)
for i in range(16):                       # 16 of the 64 steps, then an ellipsis
    rect(L+28+i*28, y+61, 24, 24, BLU, "#ffffff", 1, rounded=False)
text(L+28, y+98, "64 steps  x  32 B  =  2048 B  =  one open DRAM row", 13, GREY, 3)
text(L+28, y+118, "x 4 banks in the bankgroup   =   8 KB per dispatch", 13, BLU, 3)
rect(L+520, y+52, 156, 76, MAC, MACB, 2)
text(L+534, y+66, "290 cycles", 16, MAC, 1)
text(L+534, y+92, "measured", 12, MUT, 3)
text(L, y+192, "one dispatch = one MAC command = 64 accumulation steps in one open row",
     13, GREY, 3)

# ==================== PANEL B: one GEMV ====================
y = 330
text(L, y, "B.   how many dispatches ONE GEMV needs", 18, BLU, 1)
rect(L, y+30, 700, 130, BLU, BLUL, 1)
text(L+24, y+50, "PIMony's own benchmark  -  configs/model_configs/GEMV.json", 13, MUT, 3)
text(L+24, y+74, "4096 x 512 matrix, FP16   =   4 MB", 17, INK, 1)
text(L+24, y+104, "4 MB  /  8 KB per dispatch   =", 14, GREY, 3)
rect(L+330, y+96, 180, 44, RED, REDB, 2)
text(L+348, y+106, "512 dispatches", 18, RED, 1)
text(L, y+172, "the matrix is far bigger than one DRAM row, so a GEMV is always many dispatches",
     13, GREY, 3)

# ================ PANEL C: TODAY, one interrupt each ================
y = 540
text(L, y, "C.   TODAY  -  every dispatch fires its own interrupt", 18, RED, 1)
rect(L, y+30, W, 200, RED, REDL, 1)

MACW, HDLW, GAP = 40, 188, 6          # 40:188 is the true 290:1361 ratio
x = L + 30
for i in range(3):
    rect(x, y+66, MACW, 46, MAC, MACB, 2)
    text(x+4, y+80, "MAC", 12, MAC, 1)
    x += MACW
    rect(x, y+66, HDLW, 46, RED, REDB, 2)
    text(x+40, y+72, "trap handler", 13, RED, 1)
    text(x+40, y+92, "1361 cycles", 12, RED, 3)
    x += HDLW + GAP
text(x+10, y+80, ". . .   x 512", 18, RED, 1)

text(L+30, y+134, "each MAC block = 290 cycles of real compute", 13, MAC, 3)
text(L+30, y+154, "each red block = the CPU discovering WHICH token finished:", 13, RED, 3)
text(L+30, y+174, "   MMIO read  ->  64-iteration bitmask scan  ->  MMIO read  ->  MMIO ack",
     13, GREY, 3)
text(L+30, y+198, "512 x 1361  =  696,832 cycles  =  697 us   of pure bookkeeping",
     15, RED, 1)

# ================ PANEL D: GROUPED ================
y = 790
text(L, y, "D.   GROUPED  -  one interrupt per engine, at the end", 18, GRN, 1)
rect(L, y+30, W, 200, GRN, GRNL, 1)

x = L + 30
for i in range(10):                      # 10 of the 32, then ellipsis
    rect(x, y+66, 26, 46, MAC, MACB, 2)
    x += 26 + 3
text(x+6, y+80, ". . . x 32", 15, MAC, 1)
x += 110
rect(x, y+66, HDLW, 46, GRN, GRNB, 2)
text(x+34, y+72, "trap handler", 13, GRN, 1)
text(x+34, y+92, "ONCE per group", 12, GRN, 3)

text(L+30, y+134, "32 dispatches share one accumulator  ->  one result  ->  one completion",
     13, GREY, 3)
text(L+30, y+154, "512 dispatches / 16 engines  =  32 per group  =  16 groups  =  16 interrupts",
     13, GRN, 3)
text(L+30, y+178, "16 x 1361  =  21,776 cycles  =  22 us          (was 697 us  ->  32x less)",
     15, GRN, 1)

# ================ PANEL E: bars to scale ================
y = 1040
text(L, y, "E.   drawn to scale     (1 px = 1000 cycles)", 18, INK, 1)
rect(L, y+30, W, 250, GREY, "#f8f9fa", 1)

BX = L + 300          # bar origin
def bar(row, cycles, label, note, stroke, bg):
    yy = y + 62 + row*54
    w = max(cycles/1000.0, 3)
    text(L+24, yy+6, label, 14, stroke, 1)
    rect(BX, yy, w, 34, stroke, bg, 2)
    text(BX+w+12, yy+8, note, 13, GREY, 3)

bar(0, 696832, "handler  TODAY",   "696,832 cyc   =  697 us",  RED, REDB)
bar(1,  21776, "handler  GROUPED", "21,776 cyc   =  22 us",    GRN, GRNB)
bar(2, 148480, "MAC  (1 engine)",  "148,480 cyc  =  148 us",   MAC, MACB)
bar(3,   9280, "MAC  (16 engines)", "9,280 cyc   =  9.3 us   <- the actual compute", MAC, MACB)

text(L+24, y+286, "the red bar is the whole problem: the CPU spends 75x longer being "
     "told about the work than the memory spends doing it", 14, RED, 3)

# ================ TAKEAWAY ================
y = 1350
rect(L, y, W, 176, INK, "#ffffff", 2)
text(L+24, y+18, "WHAT THE PICTURE SAYS", 16, INK, 1)
text(L+24, y+50, "1.   a GEMV is ALWAYS many dispatches  -  the matrix does not fit in one DRAM row",
     14, GREY, 3)
text(L+24, y+76, "2.   today each one interrupts the CPU  ->  overhead grows with the work, forever",
     14, GREY, 3)
text(L+24, y+102, "3.   grouping fixes 32x of it with ONE BIT: 'last MAC' vs 'more coming'",
     14, GRN, 3)
text(L+24, y+128, "4.   what is left (22 us vs 9.3 us) is the 64-iteration scan  ->  wants a "
     "'next completed token' register", 14, BLU, 3)

doc = {"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
       "elements": elements,
       "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"}, "files": {}}
out = os.path.join(os.path.dirname(__file__), "completion_overhead.excalidraw")
with open(out, "w") as f:
    json.dump(doc, f, indent=2)
print("wrote completion_overhead.excalidraw with", len(elements), "elements")
