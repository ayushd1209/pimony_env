#!/usr/bin/env python3
"""Accurate Excalidraw diagram: the PIM I/O commands H2GWRITE / MAC / READRES.

Shows why operand-load and result-drain need dedicated commands (the Global
Buffer and the adder-tree Result are OFF the addressable cell array), the
host/device split, and the compute pair in between.

Marking convention (matches the reasoning in the discussion):
  SOLID border  = stated by the PIMony paper (§2.2 / Fig.1 / encodings)
  DASHED border = reconstruction of the *why*, corroborated by the gem5 code
File:line refs are to ext/dramsim3/PIMony/ and are verifiable in the tree.
"""
import json, itertools, os

_seed = itertools.count(2000)
def nz(): return next(_seed) * 7919 % 2147483647

elements = []

def rect(x, y, w, h, stroke="#1e1e1e", bg="transparent", fill="solid",
         sw=1, rough=1, dash="solid", rounded=True, eid=None):
    e = {
        "type": "rectangle", "id": eid or f"r{nz()}",
        "x": x, "y": y, "width": w, "height": h, "angle": 0,
        "strokeColor": stroke, "backgroundColor": bg, "fillStyle": fill,
        "strokeWidth": sw, "strokeStyle": dash, "roughness": rough,
        "opacity": 100, "groupIds": [], "frameId": None,
        "roundness": {"type": 3} if rounded else None,
        "seed": nz(), "version": 1, "versionNonce": nz(),
        "isDeleted": False, "boundElements": [], "updated": 1, "link": None,
        "locked": False,
    }
    elements.append(e)
    return e["id"]

def text(x, y, s, size=16, color="#1e1e1e", font=1, align="left", w=None):
    lines = s.split("\n")
    h = round(size * 1.25 * len(lines))
    width = w if w else max((len(l) for l in lines), default=1) * size * 0.6
    e = {
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
    }
    elements.append(e)
    return e["id"]

def arrow(pts, stroke="#1e1e1e", sw=2, dash="solid", end=True, start=False,
          rough=1):
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    x0, y0 = pts[0]
    rel = [[p[0]-x0, p[1]-y0] for p in pts]
    e = {
        "type": "arrow", "id": f"a{nz()}",
        "x": x0, "y": y0,
        "width": max(xs)-min(xs), "height": max(ys)-min(ys), "angle": 0,
        "strokeColor": stroke, "backgroundColor": "transparent",
        "fillStyle": "solid", "strokeWidth": sw, "strokeStyle": dash,
        "roughness": rough, "opacity": 100, "groupIds": [], "frameId": None,
        "roundness": {"type": 2}, "seed": nz(), "version": 1,
        "versionNonce": nz(), "isDeleted": False, "boundElements": [],
        "updated": 1, "link": None, "locked": False,
        "points": rel, "lastCommittedPoint": None,
        "startBinding": None, "endBinding": None,
        "startArrowhead": "triangle" if start else None,
        "endArrowhead": "triangle" if end else None,
    }
    elements.append(e)
    return e["id"]

# Palette (consistent with gen_excalidraw.py)
HOST_BG="#a5d8ff"; HOST_ST="#1971c2"
IN_BG="#ffe8cc";   IN_ST="#d9480f"       # H2GWRITE (operand in) - orange
MAC_BG="#ffe3e3";  MAC_ST="#e03131"      # MAC (compute) - red
OUT_BG="#d3f9d8";  OUT_ST="#2f9e44"      # READRES (result out) - green
DEV_BG="#f8f0fc";  DEV_ST="#9c36b5"      # DRAM device - purple
CELL_BG="#ffffff"; CELL_ST="#495057"
G_BG="#fff3bf";    G_ST="#e8590c"        # global buffer
RES_BG="#ebfbee";  RES_ST="#2b8a3e"      # result register
GREY="#868e96"

# ============ TITLE ============
text(120, -70, "PIM I/O commands  —  H2GWRITE / MAC / READRES  "
     "(operand-in, compute, result-out)", 26, "#1e1e1e", 1)
text(120, -34, "SOLID = stated in paper (§2.2 / Fig.1 / encodings)   "
     "·   DASHED = reconstructed 'why', corroborated by gem5 code "
     "(ext/dramsim3/PIMony, file:line refs)", 14, GREY, 3)

# ======================= 1. HOST =======================
rect(120, 20, 1440, 120, HOST_ST, HOST_BG, "solid", 2)
text(140, 32, "HOST  =  CPU  +  memory controller   "
     "(RISC-V core + gem5 memory system)", 19, HOST_ST, 1)
text(140, 66, "supplies the per-query INPUT vector; wants the computed RESULT "
     "back", 13, "#1e1e1e", 3)
# reconstructed 'forced' note, dashed underline via separate note box
rect(140, 92, 1400, 34, HOST_ST, "transparent", "solid", 1, dash="dashed")
text(150, 99, "The only wire into the DRAM die is a command on the C/A bus → "
     "loading G, running MAC, and draining the result are ALL "
     "host-issued commands (forced, not a design choice).", 12, "#1971c2", 3)

arrow([(840,140),(840,230)], "#1e1e1e", 2)
text(852, 168, "C/A command bus", 13, "#1e1e1e", 3)
text(852, 186, "(new PIM cmds multiplexed onto existing pins)", 11, GREY, 3)

# ======================= 2. THE THREE COMMANDS =======================
# H2GWRITE (operand in) -- aligned above Global Buffer G
rect(180, 230, 360, 250, IN_ST, IN_BG, "solid", 3)
text(196, 240, "H2GWRITE", 18, IN_ST, 1)
text(196, 266, "operand  IN", 14, "#1e1e1e", 1)
text(196, 292, "host  →  Global Buffer (G)", 12, "#495057", 3)
text(196, 312, "no DRAM row opened (no ACT)", 12, "#495057", 3)
text(196, 332, "t = tCCD_S × num_macs", 13, IN_ST, 3)
text(196, 356, "channel_state.cc:266", 11, GREY, 3)
text(196, 372, "pim_controller.cc:656", 11, GREY, 3)
text(196, 388, "LLM.cc:192  (addr = ch,0,0,0,0,0", 11, GREY, 3)
text(196, 404, "            → not cell-addressed)", 11, GREY, 3)
rect(196, 428, 328, 44, IN_ST, "transparent", "solid", 1, dash="dashed")
text(204, 434, "D2GWRITE = same, but source is a DRAM", 11, "#495057", 3)
text(204, 450, "row → pays tRP + tRCDRD to open it first", 11, "#495057", 3)

# MAC (compute) -- aligned above MAC engine
rect(630, 230, 380, 250, MAC_ST, MAC_BG, "solid", 3)
text(646, 240, "MAC  (+ MACINTR)", 18, MAC_ST, 1)
text(646, 266, "COMPUTE", 14, "#1e1e1e", 1)
text(646, 292, "weights × input, accumulate", 12, "#495057", 3)
text(646, 312, "engages engine: mac_states,", 12, "#495057", 3)
text(646, 328, "   remaining_macs (stateful)", 12, "#495057", 3)
text(646, 348, "fires MACINTR → completion IRQ", 12, MAC_ST, 3)
text(646, 372, "t = tCCD_L × (num_macs + 5)", 13, MAC_ST, 3)
text(646, 396, "num_macs = 256-bit column STEPS", 11, GREY, 3)
text(646, 420, "pim_controller.cc:662-682", 11, GREY, 3)
text(646, 436, "LLM.cc:223 (addr = ch,rank,bg,row →", 11, GREY, 3)
text(646, 452, "            fully array-addressed)", 11, GREY, 3)

# READRES (result out) -- aligned above Result register
rect(1100, 230, 380, 250, OUT_ST, OUT_BG, "solid", 3)
text(1116, 240, "READRES", 18, OUT_ST, 1)
text(1116, 266, "result  OUT", 14, "#1e1e1e", 1)
text(1116, 292, "drain adder-tree result → host", 12, "#495057", 3)
text(1116, 312, "no arithmetic; num_macs = 0", 12, "#495057", 3)
text(1116, 332, "t = read_delay", 13, OUT_ST, 3)
text(1116, 352, "\"readres delay == read delay\"", 11, GREY, 3)
text(1116, 376, "pim_controller.cc:653-655", 11, GREY, 3)
text(1116, 392, "LLM.cc:209  (num_macs = 0)", 11, GREY, 3)
rect(1116, 420, 348, 52, OUT_ST, "transparent", "solid", 1, dash="dashed")
text(1124, 426, "the accumulated sum lives in the adder", 11, "#495057", 3)
text(1124, 442, "tree, NOT in a DRAM cell — a normal READ", 11, "#495057", 3)
text(1124, 458, "there returns weights, not the result.", 11, "#495057", 3)

# left-to-right compute flow between the commands
arrow([(540,300),(630,300)], "#1e1e1e", 3)
text(548, 274, "then", 12, "#1e1e1e", 3)
arrow([(1010,300),(1100,300)], "#1e1e1e", 3)
text(1020, 274, "then", 12, "#1e1e1e", 3)

# ======================= 3. DRAM DEVICE =======================
rect(120, 560, 1440, 500, DEV_ST, DEV_BG, "solid", 3)
text(140, 572, "DRAM DEVICE  (die)  =  cell array  +  PIM silicon", 19, DEV_ST, 1)
text(140, 600, "everything below is on the far side of the C/A/DQ interface; "
     "PIMony does not modify that interface", 12, "#495057", 3)

# Global Buffer G (target of H2GWRITE)
rect(180, 650, 360, 150, G_ST, G_BG, "solid", 2)
text(196, 660, "Global Buffer  (G)", 15, G_ST, 1)
text(196, 686, "holds the INPUT vector;", 12, "#495057", 3)
text(196, 704, "broadcasts it to every bank for", 12, "#495057", 3)
text(196, 720, "parallel MAC", 12, "#495057", 3)
rect(196, 742, 328, 50, G_ST, "transparent", "solid", 1, dash="dashed")
text(204, 748, "NOT part of the addressable cell array", 11, IN_ST, 3)
text(204, 764, "gem5: timing-only, ~1 page wide", 11, GREY, 3)
text(204, 778, "(gwrite_delay=tCCD_S×page/read, config.cc:379)", 10, GREY, 3)

# Cell array (weights) -- below G
rect(180, 830, 360, 190, CELL_ST, CELL_BG, "solid", 2)
text(196, 840, "DRAM cell array", 15, "#1e1e1e", 1)
text(196, 866, "WEIGHTS resident here as", 12, "#495057", 3)
text(196, 884, "ordinary data (written once)", 12, "#495057", 3)
text(196, 908, "reachable by normal READ / WRITE", 12, "#2b8a3e", 3)
rect(196, 934, 328, 74, CELL_ST, "transparent", "solid", 1, dash="dashed")
text(204, 940, "Operand asymmetry (§2.2): weights are", 11, "#495057", 3)
text(204, 956, "stationary → no load command needed;", 11, "#495057", 3)
text(204, 972, "the input is transient → must be pushed", 11, "#495057", 3)
text(204, 988, "into G every GEMV via H2GWRITE.", 11, "#495057", 3)

# MAC engine + adder tree
rect(630, 720, 380, 300, MAC_ST, "#fff5f5", "solid", 2)
text(646, 730, "MAC units + adder tree", 15, MAC_ST, 1)
text(646, 756, "one per (rank × bankgroup), ASYNC", 12, "#495057", 3)
text(646, 776, "multiply weights × input,", 12, "#495057", 3)
text(646, 792, "accumulate through adder tree", 12, "#495057", 3)
text(646, 816, "= the paper's \"Result\" path (Fig.1)", 12, "#495057", 3)
rect(646, 844, 348, 40, MAC_ST, "transparent", "solid", 1, dash="dashed")
text(654, 850, "gem5 models timing only — no actual", 11, GREY, 3)
text(654, 866, "arithmetic on the backing-store bytes.", 11, GREY, 3)

# Result register (target of READRES)
rect(1100, 650, 380, 150, RES_ST, RES_BG, "solid", 2)
text(1116, 660, "Result  (adder-tree output)", 15, RES_ST, 1)
text(1116, 686, "accumulated sum per bankgroup", 12, "#495057", 3)
text(1116, 704, "under BG-count scheme", 12, "#495057", 3)
rect(1116, 730, 348, 60, RES_ST, "transparent", "solid", 1, dash="dashed")
text(1124, 736, "NOT a memory cell → unreachable by a", 11, OUT_ST, 3)
text(1124, 752, "normal READ. READRES is the only way", 11, OUT_ST, 3)
text(1124, 768, "to drain it back to the host.", 11, OUT_ST, 3)

# ---- command -> structure arrows (color coded) ----
arrow([(360,480),(360,650)], IN_ST, 3)
text(372, 545, "load input", 12, IN_ST, 3)
text(372, 561, "vector", 12, IN_ST, 3)

arrow([(820,480),(820,720)], MAC_ST, 3)
text(832, 585, "launch MAC", 12, MAC_ST, 3)

arrow([(1290,650),(1290,480)], OUT_ST, 3)   # result flows UP/out
text(1302, 545, "drain result", 12, OUT_ST, 3)
text(1302, 561, "to host", 12, OUT_ST, 3)

# ---- internal device dataflow ----
arrow([(360,800),(360,830)], G_ST, 2)  # G sits above cell array visually; broadcast link to MAC below
arrow([(540,760),(630,800)], G_ST, 2)
text(548, 748, "broadcast to all banks", 11, G_ST, 3)

arrow([(540,900),(630,900)], "#2b8a3e", 2)
text(548, 872, "weights fetched by MAC", 11, "#2b8a3e", 3)
text(548, 888, "(ordinary array read)", 11, "#2b8a3e", 3)

arrow([(1010,800),(1100,760)], MAC_ST, 2)
text(1018, 812, "accumulated sum", 11, MAC_ST, 3)

# ======================= 4. THESIS SEAM =======================
rect(120, 1090, 1440, 90, "#1971c2", "#e7f5ff", "solid", 2, dash="dashed")
text(140, 1100, "Thesis seam  (the invisibility these primitives target)", 15,
     "#1971c2", 1)
text(140, 1128, "To the host, \"load the PIM input\" and \"drain the result\" are "
     "just MMIO store / load that a driver silently lowers to these DRAM "
     "commands.", 12, "#495057", 3)
text(140, 1150, "There is no ISA vocabulary for either → a compiler cannot "
     "reason about the data movement or its ordering. That gap is what the "
     "custom instructions sit in front of.", 12, "#495057", 3)

doc = {
    "type": "excalidraw", "version": 2,
    "source": "https://excalidraw.com",
    "elements": elements,
    "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
    "files": {},
}
out = os.path.join(os.path.dirname(__file__), "PIM_io_commands.excalidraw")
with open(out, "w") as f:
    json.dump(doc, f, indent=2)
print("wrote", out, "with", len(elements), "elements")
