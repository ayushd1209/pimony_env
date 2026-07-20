#!/usr/bin/env python3
"""LPDDR5X device-hierarchy diagram (channel -> rank -> bankgroup -> bank -> row/col).

Every number is taken verbatim from:
  gem5/ext/dramsim3/PIMony/PIMSim/configs/LPDDR5X_12Gb_x16_8533_pimony.ini
(selected by gem5/configs/pimony/pimony_mem.json).
"""
import json, itertools, os

_seed = itertools.count(1000)
def nz(): return next(_seed) * 7919 % 2147483647

elements = []

def rect(x, y, w, h, stroke="#1e1e1e", bg="transparent", fill="solid",
         sw=1, rough=1, dash="solid", rounded=True, eid=None):
    elements.append({
        "type": "rectangle", "id": eid or f"r{nz()}",
        "x": x, "y": y, "width": w, "height": h, "angle": 0,
        "strokeColor": stroke, "backgroundColor": bg, "fillStyle": fill,
        "strokeWidth": sw, "strokeStyle": dash, "roughness": rough,
        "opacity": 100, "groupIds": [], "frameId": None,
        "roundness": {"type": 3} if rounded else None,
        "seed": nz(), "version": 1, "versionNonce": nz(),
        "isDeleted": False, "boundElements": [], "updated": 1, "link": None,
        "locked": False,
    })

def text(x, y, s, size=16, color="#1e1e1e", font=1, align="left", w=None):
    lines = s.split("\n")
    h = round(size * 1.25 * len(lines))
    width = w if w else max((len(l) for l in lines), default=1) * size * 0.6
    elements.append({
        "type": "text", "id": f"t{nz()}",
        "x": x, "y": y, "width": width, "height": h, "angle": 0,
        "strokeColor": color, "backgroundColor": "transparent",
        "fillStyle": "solid", "strokeWidth": 1, "strokeStyle": "solid",
        "roughness": 1, "opacity": 100, "groupIds": [], "frameId": None,
        "roundness": None, "seed": nz(), "version": 1, "versionNonce": nz(),
        "isDeleted": False, "boundElements": [], "updated": 1, "link": None,
        "locked": False, "fontSize": size, "fontFamily": font,
        "text": s, "textAlign": align, "verticalAlign": "top",
        "containerId": None, "originalText": s, "lineHeight": 1.25,
        "baseline": size,
    })

def arrow(pts, stroke="#1e1e1e", sw=2, dash="solid", end=True, start=False):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    x0, y0 = pts[0]
    elements.append({
        "type": "arrow", "id": f"a{nz()}",
        "x": x0, "y": y0, "width": max(xs)-min(xs), "height": max(ys)-min(ys),
        "angle": 0, "strokeColor": stroke, "backgroundColor": "transparent",
        "fillStyle": "solid", "strokeWidth": sw, "strokeStyle": dash,
        "roughness": 1, "opacity": 100, "groupIds": [], "frameId": None,
        "roundness": {"type": 2}, "seed": nz(), "version": 1,
        "versionNonce": nz(), "isDeleted": False, "boundElements": [],
        "updated": 1, "link": None, "locked": False,
        "points": [[p[0]-x0, p[1]-y0] for p in pts], "lastCommittedPoint": None,
        "startBinding": None, "endBinding": None,
        "startArrowhead": "triangle" if start else None,
        "endArrowhead": "triangle" if end else None,
    })

# palette
CH="#1971c2";  CHB="#a5d8ff"      # channel   (parallel)
RK="#e8590c";  RKB="#ffe8cc"      # rank      (serial)
BG="#2f9e44";  BGB="#d3f9d8"      # bankgroup
BK="#0b7285";  BKB="#c5f6fa"      # bank
AR="#862e9c";  ARB="#f3d9fa"      # array/row/col
DIM="#adb5bd"
GREY="#495057"; MUT="#868e96"

MAINX, MAINW = 60, 900
zoomx = MAINX + MAINW/2

def zoom(y, label):
    arrow([(zoomx, y), (zoomx, y+38)], "#1e1e1e", 2, dash="dashed")
    text(zoomx+14, y+8, label, 13, MUT, 3)

# ---------------- title ----------------
text(MAINX, -80, "LPDDR5X memory hierarchy  —  channel down to row & column",
     26, "#1e1e1e", 1)
text(MAINX, -44, "config: LPDDR5X_12Gb_x16_8533_pimony.ini   (via configs/pimony/"
     "pimony_mem.json)   —   every count/timing below is from this file",
     13, MUT, 3)

# ---------------- L0 subsystem ----------------
rect(MAINX, 10, MAINW, 70, "#1e1e1e", "#f8f9fa", "solid", 2)
text(MAINX+16, 22, "LPDDR5X device  —  soldered BGA package (NOT a DIMM, no slots, no 'sides')",
     16, "#1e1e1e", 1)
text(MAINX+16, 50, "protocol=LPDDR5X   8533 MT/s   tCK=0.9376465 ns   WCK:CK=4:1",
     13, GREY, 3)

zoom(80, "the package exposes...")

# ---------------- L1 channels (parallel) ----------------
rect(MAINX, 128, MAINW, 150, CH, CHB, "solid", 3)
text(MAINX+16, 138, "LEVEL 1 — CHANNELS  ×4   (channels=4)", 17, CH, 1)
text(MAINX+16, 164, "PARALLEL: 4 independent channels, x16 data path to each die; all active same cycle",
     12, GREY, 3)
cw = 200
for i in range(4):
    cx = MAINX + 20 + i*(cw+10)
    hi = (i == 0)
    rect(cx, 196, cw, 66, CH, "#ffffff" if not hi else CHB,
         "solid", 3 if hi else 1)
    text(cx+14, 208, f"Channel {i}", 15, "#1e1e1e" if hi else GREY, 1)
    text(cx+14, 232, "x16 to die", 11, MUT, 3)
    if hi:
        text(cx+14, 246, "▶ drilling in", 11, CH, 3)

zoom(278, "one channel carries...")

# ---------------- L2 ranks (serial) ----------------
rect(MAINX, 326, MAINW, 150, RK, RKB, "solid", 3)
text(MAINX+16, 336, "LEVEL 2 — Channel 0 : RANKS ×2   (ranks=2, per channel)",
     17, RK, 1)
text(MAINX+16, 362, "SERIAL: both ranks share the one 32-bit bus, selected by chip-select — they TAKE TURNS",
     12, GREY, 3)
for i in range(2):
    rx = MAINX + 30 + i*430
    hi = (i == 0)
    rect(rx, 394, 400, 66, RK if hi else DIM, RKB if hi else "#f1f3f5",
         "solid", 3 if hi else 1)
    text(rx+14, 406, f"Rank {i}", 15, "#1e1e1e" if hi else MUT, 1)
    text(rx+14, 430, "ONE x16 die = 12 Gbit  (LPDDR → devices_per_rank=1, configuration.cc:82)"
         if hi else "idle while Rank 0 owns the bus", 11, MUT, 3)

zoom(476, "each rank contains...")

# ---------------- L3 bankgroups ----------------
rect(MAINX, 524, MAINW, 140, BG, BGB, "solid", 3)
text(MAINX+16, 534, "LEVEL 3 — Rank 0 : BANKGROUPS ×4   (bankgroups=4)", 17, BG, 1)
text(MAINX+16, 560, "cross-group access is cheap (tCCD_S=2 CK); same-group pays tCCD_L=4 CK",
     12, GREY, 3)
gw = 200
for i in range(4):
    gx = MAINX + 20 + i*(gw+10)
    hi = (i == 0)
    rect(gx, 588, gw, 60, BG, "#ffffff" if not hi else BGB, "solid", 3 if hi else 1)
    text(gx+14, 606, f"Bankgroup {i}", 14, "#1e1e1e" if hi else GREY, 1)

zoom(664, "each bankgroup holds...")

# ---------------- L4 banks ----------------
rect(MAINX, 712, MAINW, 140, BK, BKB, "solid", 3)
text(MAINX+16, 722, "LEVEL 4 — Bankgroup 0 : BANKS ×4   (banks_per_group=4  →  16 banks/rank)",
     17, BK, 1)
text(MAINX+16, 748, "a bank is an independent 2-D cell array with ONE row buffer (only 1 open row at a time)",
     12, GREY, 3)
bw = 200
for i in range(4):
    bx = MAINX + 20 + i*(bw+10)
    hi = (i == 0)
    rect(bx, 776, bw, 60, BK, "#ffffff" if not hi else BKB, "solid", 3 if hi else 1)
    text(bx+14, 794, f"Bank {i}", 14, "#1e1e1e" if hi else GREY, 1)

zoom(852, "inside one bank...")

# ---------------- L5 the cell array (rows x columns) ----------------
rect(MAINX, 900, MAINW, 360, AR, "#fcf4ff", "solid", 3)
text(MAINX+16, 910, "LEVEL 5 — Bank 0 : the 2-D cell array (rows × columns)", 17, AR, 1)

ax, ay, aw, ah = MAINX+150, 946, 500, 210
rect(ax, ay, aw, ah, AR, "#ffffff", "solid", 2)
# faint grid
for i in range(1, 8):
    arrow([(ax, ay+ah*i/8), (ax+aw, ay+ah*i/8)], "#e9d8f4", 1, end=False)
for j in range(1, 12):
    arrow([(ax+aw*j/12, ay), (ax+aw*j/12, ay+ah)], "#e9d8f4", 1, end=False)
# highlighted open row
rect(ax, ay+ah*3/8, aw, ah/8, "#e8590c", "#ffe8cc", "solid", 2)
text(ax+8, ay+ah*3/8+2, "open row  →  ACTIVATE (tRCD=19 CK) copies it into the row buffer",
     11, "#d9480f", 3)
# highlighted column cell
rect(ax+aw*7/12, ay, aw/12, ah, "#1971c2", "#d0ebff", "solid", 2)
# axis labels
text(MAINX+16, ay+70, "rows", 14, AR, 1)
text(MAINX+16, ay+92, "= 49152", 13, GREY, 3)
text(MAINX+16, ay+110, "(rows)", 11, MUT, 3)
text(MAINX+16, ay+134, "subarray", 12, MUT, 3)
text(MAINX+16, ay+150, "= 512 rows", 11, MUT, 3)
text(MAINX+16, ay+164, "(rows_in_", 11, MUT, 3)
text(MAINX+16, ay+178, " subarray)", 11, MUT, 3)
text(ax+aw*7/12-4, ay-20, "1 column", 12, CH, 3)
text(ax+120, ay-20, "columns = 1024  (one row = 1024 cols)", 12, GREY, 3)

# row buffer bar
rect(ax, ay+ah+14, aw, 34, "#e8590c", "#fff3bf", "solid", 2)
text(ax+10, ay+ah+20, "ROW BUFFER / sense amps  =  2 KiB page  (1024 cols × 16b = 2048 B)",
     12, "#d9480f", 3)
text(ax, ay+ah+56, "READ/WRITE selects columns from the open row — BL=16 → 32 B access "
     "(dram_req_size=32); tCL(read)=23 CK, tCCD gates back-to-back column cmds",
     11, GREY, 3)

# ================= RIGHT INFO PANEL =================
px, pw = 1000, 460

# exact numbers
rect(px, 10, pw, 300, "#1e1e1e", "#f8f9fa", "solid", 2)
text(px+16, 20, "EXACT CONFIG  (LPDDR5X_..8533_pimony.ini)", 15, "#1e1e1e", 1)
rows = [
    ("[dram_structure]", ""),
    ("protocol", "LPDDR5X"),
    ("channels", "4"),
    ("ranks (per channel)", "2"),
    ("bankgroups (per rank)", "4"),
    ("banks_per_group", "4"),
    ("rows", "49152"),
    ("rows_in_subarray", "512"),
    ("columns", "1024"),
    ("device_width", "16  (x16 die)"),
    ("bus_width", "32  (n/a: LPDDR uses"),
    ("", "      device_width)"),
    ("BL", "16"),
    ("tCK", "0.9376465 ns"),
]
yy = 48
for k, v in rows:
    if v == "":
        text(px+16, yy, k, 12, AR, 3); yy += 18
    else:
        text(px+28, yy, k, 12, GREY, 3)
        text(px+300, yy, v, 12, "#1e1e1e", 3)
        yy += 18

# totals
rect(px, 326, pw, 128, BK, BKB, "solid", 2)
text(px+16, 336, "TOTALS  (multiply down the tree)", 14, BK, 1)
text(px+16, 362, "channels                        = 4", 12, GREY, 3)
text(px+16, 380, "ranks    = 4 × 2               = 8", 12, GREY, 3)
text(px+16, 398, "bankgroups = 8 × 4            = 32", 12, GREY, 3)
text(px+16, 416, "banks    = 32 × 4             = 128", 12, "#1e1e1e", 1)
text(px+16, 434, "capacity: 1 rank = 1 x16 die = 12 Gbit → 4ch×2 = 12 GB",
     11, MUT, 3)

# parallel vs serial
rect(px, 470, pw, 96, RK, RKB, "solid", 2)
text(px+16, 480, "PARALLEL vs SERIAL (what PIM can overlap)", 14, RK, 1)
text(px+16, 506, "PARALLEL  : channels ×4  — true concurrent MACs", 12, CH, 3)
text(px+16, 524, "SERIAL    : ranks ×2   — share bus, hide latency only", 12, GREY, 3)
text(px+16, 542, "PER-BANK  : 16/rank — async engine overlaps within a rank", 12, BG, 3)

# address mapping decode
rect(px, 582, pw, 210, AR, ARB, "solid", 2)
text(px+16, 592, "ADDRESS MAP:  address_mapping = rorabacobgch", 14, AR, 1)
text(px+16, 618, "field order high→low bits:", 12, GREY, 3)
fields = [
    ("ro", "row", "49152 → 16 bits"),
    ("ra", "rank", "2 → 1 bit"),
    ("ba", "bank", "4 → 2 bits"),
    ("co", "column", "1024 → 10 bits"),
    ("bg", "bankgroup", "4 → 2 bits"),
    ("ch", "channel", "4 → 2 bits (LOWEST)"),
]
yy = 640
for tag, name, bits in fields:
    text(px+28, yy, tag, 12, AR, 3)
    text(px+70, yy, name, 12, "#1e1e1e", 3)
    text(px+200, yy, bits, 11, MUT, 3)
    yy += 18
text(px+16, yy+2, "channel = lowest bits → consecutive lines spray", 11, CH, 3)
text(px+16, yy+18, "across all 4 channels first (max parallelism)", 11, CH, 3)

doc = {
    "type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
    "elements": elements,
    "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
    "files": {},
}
with open(os.path.join(os.path.dirname(__file__), "LPDDR5X_hierarchy.excalidraw"), "w") as f:
    json.dump(doc, f, indent=2)
print("wrote LPDDR5X_hierarchy.excalidraw with", len(elements), "elements")
