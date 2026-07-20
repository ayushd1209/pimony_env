#!/usr/bin/env python3
"""Corrected GEMV-on-PIM picture: one DRAM row -> 64 MAC steps (M0..M63).

Fixes the hand-drawn version:
  nMAC = 64 (steps, WORK)  — NOT 32 (that's chip-wide ENGINES, a different axis)
  256 bits per chunk = 16 FP16 elements (not "256 elements")
  each step = 16 FP16 MACs, charged tCCD_L = 4 CK in the model (not "16 cycles")
Numbers from LPDDR5X_12Gb_x16_8533_pimony.ini.
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

def rect(x, y, w, h, stroke, bg="transparent", sw=1, dash="solid"):
    shape("rectangle", x, y, w, h, stroke, bg, sw, dash)

def ellipse(x, y, w, h, stroke, bg, sw=2):
    shape("ellipse", x, y, w, h, stroke, bg, sw)

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

GRN="#2f9e44"; GRNB="#d3f9d8"; GRNL="#ebfbee"
PNK="#e03131"; PNKB="#ffe3e3"; PNKL="#fff5f5"
MAC="#0ca678"; MACB="#c3fae8"
RED="#e03131"; GREY="#495057"; MUT="#868e96"; BLU="#1971c2"

# chunk geometry (mirror the user's: 3 explicit chunks + remainder + M63)
chunks = [("c0", 320), ("c1", 440), ("c2", 560)]   # x of first three
CW = 120
REM_X, REM_W = 680, 620
C63_X = 1300
centers = [x + CW/2 for _, x in chunks] + [C63_X + CW/2]   # M0,M1,M2, M63

# ---------------- title ----------------
text(300, -78, "GEMV on PIM  —  one DRAM row → 64 MAC steps", 24, "#1e1e1e", 1)
text(300, -46, "config: LPDDR5X_12Gb_x16_8533_pimony.ini   —   corrected version of the "
     "hand sketch (nMAC=64, 16 FP16/step, tCCD_L timing)", 12, MUT, 3)

# ---------------- TOP operand A ----------------
text(320, 22, "OPERAND A — one DRAM row = 2 KB = 16384 bits (activated into the row buffer)",
     15, GRN, 1)
for _, x in chunks:
    rect(x, 52, CW, 88, GRN, GRNB, 2)
    text(x+34, 88, "256b", 14, "#1e1e1e", 3)
rect(REM_X, 52, REM_W, 88, GRN, GRNL, 1)
text(REM_X+180, 88, "… 60 more 256b chunks …", 14, MUT, 3)
rect(C63_X, 52, CW, 88, GRN, GRNB, 2)
text(C63_X+34, 88, "256b", 14, "#1e1e1e", 3)

# middle definition line
text(320, 168, "1 chunk  =  256 bits  =  32 bytes  =  16 FP16 elements  =  1 MAC step",
     15, RED, 3)

# ---------------- MAC step circles ----------------
MY, MH, MW = 250, 66, 84
labels = ["M0", "M1", "M2", "M63"]
for i, cx in enumerate(centers):
    ellipse(cx-MW/2, MY, MW, MH, MAC, MACB, 2)
    text(cx-14, MY+22, labels[i], 15, "#0b7285", 1)
text(760, MY+18, "…", 26, MAC, 1)

# arrows: A(top) down into circle, B(bottom) up into circle
for cx in centers:
    arrow([(cx, 140), (cx, MY-2)], GRN, 2)          # A -> M
    arrow([(cx, 470), (cx, MY+MH+2)], PNK, 2)       # B -> M

# nMAC callout
text(40, MY-40, "nMAC = 64", 22, RED, 1)
text(40, MY-8, "steps to finish", 13, GREY, 3)
text(40, MY+8, "ONE row = WORK", 13, GREY, 3)
arrow([(230, MY+30), (centers[0]-MW/2-6, MY+30)], RED, 2)

# step cost line
text(320, 350, "each step = 16 FP16 multiply-adds  →  charged tCCD_L = 4 CK (~3.75 ns) "
     "in the model (no real arithmetic, timing only)", 13, GREY, 3)

# ---------------- BOTTOM operand B ----------------
for _, x in chunks:
    rect(x, 472, CW, 88, PNK, PNKB, 2)
    text(x+34, 508, "256b", 14, "#1e1e1e", 3)
rect(REM_X, 472, REM_W, 88, PNK, PNKL, 1)
text(REM_X+180, 508, "… 60 more 256b chunks …", 14, MUT, 3)
rect(C63_X, 472, CW, 88, PNK, PNKB, 2)
text(C63_X+34, 508, "256b", 14, "#1e1e1e", 3)
text(320, 574, "OPERAND B — 16 FP16 numbers per chunk feed each step (× and accumulate)",
     15, PNK, 1)

# ---------------- WORK vs WORKERS box ----------------
rect(320, 626, 1100, 150, "#1e1e1e", "#f8f9fa", 2)
text(340, 636, "TWO DIFFERENT AXES — do not mix (this was the confusion)", 15, "#1e1e1e", 1)
text(340, 666, "WORK     : 64 MAC steps to finish ONE row   = nMAC   ← THIS diagram",
     13, GRN, 3)
text(340, 686, "WORKERS  : 32 MAC engines in the whole chip (8 per channel × 4 channels, "
     "one per bankgroup)", 13, BLU, 3)
text(340, 706, "           → the 32 is chip-wide hardware, NOT the step count on this row",
     12, MUT, 3)
text(340, 732, "one engine runs its 64 steps in sequence: 64 × 4 CK ≈ 256 CK ≈ 240 ns/row",
     13, GREY, 3)
text(340, 752, "up to 32 such rows crunched in parallel across the chip (one per engine)",
     13, GREY, 3)

# ---------------- what changed vs sketch ----------------
rect(320, 792, 1100, 96, PNK, PNKL, 1)
text(340, 800, "FIXED FROM YOUR SKETCH", 13, PNK, 1)
text(340, 824, "nMAC = 32  →  64        |   '256 elements'  →  256 bits = 16 FP16 elements",
     12, GREY, 3)
text(340, 844, "'16 cycles of MAC'  →  16 FP16 elements/step; model charges tCCD_L = 4 CK",
     12, GREY, 3)
text(340, 864, "'LPDD5'  →  LPDDR5X    |   note: full row = K of 1024 FP16 (1x128x128 fills only 8 steps)",
     12, GREY, 3)

doc = {"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
       "elements": elements,
       "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"}, "files": {}}
with open(os.path.join(os.path.dirname(__file__), "MAC_steps.excalidraw"), "w") as f:
    json.dump(doc, f, indent=2)
print("wrote MAC_steps.excalidraw with", len(elements), "elements")
