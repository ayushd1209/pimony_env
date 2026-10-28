#!/usr/bin/env python3
"""Instruction encodings and the Request::extraData payload for the thesis.

RISC-V spec layout: bit indices over the field boxes, field widths under them,
then one row of values per instruction with its mnemonic on the right.
Traced to code: decoder.isa custom-0 block (decoded on funct3) and bitfields.isa
(PIMCOMP = bit 25). Every encoding row is re-assembled and checked against the .word the
test programs execute (pim_group/main.c, fence_baremetal/main.c, gpt3.c).
Run: python3 gen_encodings.py  ->  pim_{dispatch,gemv,wait,fence}_encoding.excalidraw
                                  pim_payload.excalidraw
"""
import json, itertools, os

SANS = 2                               # Excalidraw: Helvetica; one font, as in the spec
INK, GREY = "#1e1e1e", "#495057"

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
         "fillStyle": "solid", "strokeWidth": 1.5, "strokeStyle": "solid",
         "roughness": 0, "opacity": 100, "groupIds": ["encoding"], "frameId": None,
         "roundness": None, "seed": n * 7919 % 2147483647, "version": 1,
         "versionNonce": n * 104729 % 2147483647, "isDeleted": False,
         "boundElements": [], "updated": 1, "link": None, "locked": False}
    e.update(kw)
    els.append(e)
    return e

def rect(x, y, w, h):
    return _base("rectangle", x, y, w, h, backgroundColor="#ffffff")

def text(x, y, s, size, color=INK, align="left", box=None):
    """x is the left edge (align=left), the centre (center) or the right edge (right)."""
    w = text_w(s, size)
    h = size * 1.25 * len(s.split("\n"))
    left = {"left": x, "center": x - w / 2, "right": x - w}[align]
    if box and (left < box[0] - 0.5 or left + w > box[1] + 0.5):
        warnings.append(f"overflow: {s!r} [{left:.0f}..{left + w:.0f}] not in [{box[0]:.0f}..{box[1]:.0f}]")
    return _base("text", left, y, w, h, strokeColor=color, fontSize=size, fontFamily=SANS,
                 text=s, originalText=s, textAlign=align, verticalAlign="top",
                 containerId=None, lineHeight=1.25, baseline=round(size * 0.9), autoResize=True)

def ctext(cx, cy, s, size, color=INK, box=None):
    """Text centred on (cx, cy)."""
    h = size * 1.25 * len(s.split("\n"))
    return text(cx, cy - h / 2, s, size, color, "center", box)

# ---------------------------------------------------------------- encodings
R_TYPE = [("funct7", 31, 25), ("rs2", 24, 20), ("rs1", 19, 15), ("funct3", 14, 12),
          ("rd", 11, 7), ("opcode", 6, 0)]
I_TYPE = [("imm[11:0]", 31, 20), ("rs1", 19, 15), ("funct3", 14, 12), ("rd", 11, 7),
          ("opcode", 6, 0)]
DISPATCH = [("funct6", 31, 26), ("c", 25, 25)] + R_TYPE[1:]   # funct7 split, as AMO aq/rl

# name: (format, fields, rows); row = (values, mnemonic, .word with rd=a2 rs1=a0 rs2=a1 c=1)
FIGS = {
    "pim_dispatch": ("R-type", DISPATCH, [
        (["000000", "comp", "num_macs", "addr", "011", "token", "0001011"], "pim.dispatch", 0x02B5360B)]),
    "pim_gemv": ("R-type", R_TYPE, [
        (["0000000", "shape", "desc_ptr", "101", "token", "0001011"], "pim.gemv", 0x00B5560B)]),
    "pim_wait": ("I-type", I_TYPE, [
        (["000000000000", "token", "000", "status", "0001011"], "pim.wait", 0x0005060B)]),
    "pim_fence": ("I-type", I_TYPE, [
        (["000000000000", "addr", f3, "00000", "0001011"], m, w) for f3, m, w in (
            ("001", "pim.fence.cl", 0x0005100B), ("010", "pim.fence.inv", 0x0005200B),
            ("100", "pim.fence.flush", 0x0005400B))]),
}
OPERAND = {"rd": 12, "rs1": 10, "rs2": 11, "c": 1}            # a2, a0, a1, completing

def is_bits(v):
    return set(v) <= {"0", "1"}

def assemble(fields, vals):
    word = 0
    for (name, msb, lsb), v in zip(fields, vals):
        assert not is_bits(v) or len(v) == msb - lsb + 1, (name, v)
        word |= (int(v, 2) if is_bits(v) else OPERAND[name]) << lsb
    return word

# ---------------------------------------------------------------- layout
U, MINW = 22, 46                       # px per bit; a 1-bit box still fits "comp"
WORD = 32 * U
BOX_T, BOX_H = 22, 42                  # field boxes, bit indices above
ROW = 27                               # value-row pitch
COL = WORD + 26                        # format / mnemonic column
MNEM = 17
COL_W = max(text_w(m, MNEM) for _, _, rows in FIGS.values() for _, m, _ in rows)

def figure(name, fmt, fields, rows):
    els.clear()
    bits = [msb - lsb + 1 for _, msb, lsb in fields]
    u = (WORD - MINW * bits.count(1)) / (32 - bits.count(1))
    xs = [0]
    for b in bits:
        xs.append(xs[-1] + (MINW if b == 1 else b * u))
    box_b = BOX_T + BOX_H
    for (field, msb, lsb), b, x0, x1 in zip(fields, bits, xs, xs[1:]):
        rect(x0, BOX_T, x1 - x0, BOX_H)
        ctext((x0 + x1) / 2, BOX_T + BOX_H / 2, field, 18, box=(x0 + 3, x1 - 3))
        if b == 1:
            text((x0 + x1) / 2, 0, str(msb), 15, align="center")
        else:
            text(x0 + 3, 0, str(msb), 15)
            text(x1 - 3, 0, str(lsb), 15, align="right")
        ctext((x0 + x1) / 2, box_b + 13, str(b), 15, color=GREY)
    ctext(COL + text_w(fmt, 18) / 2, BOX_T + BOX_H / 2, fmt, 18)
    for r, (vals, mnem, want) in enumerate(rows):
        got = assemble(fields, vals)
        assert got == want, f"{mnem}: assembles to {got:#010x}, tests emit {want:#010x}"
        cy = box_b + 41 + r * ROW
        for v, x0, x1 in zip(vals, xs, xs[1:]):
            ctext((x0 + x1) / 2, cy, v, 17, box=(x0, x1))
        text(COL, cy - MNEM * 1.25 / 2, mnem, MNEM)
    # invisible spacer: every figure gets the same width, so the same scale at \textwidth
    _base("rectangle", COL, BOX_T, COL_W, BOX_H, strokeColor="transparent")
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), f"{name}_encoding.excalidraw")
    json.dump({"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
               "elements": els, "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
               "files": {}}, open(out, "w"), indent=1)
    print(f"wrote {out}: {len(els)} elements")

def payload_figure():
    """64-bit payload packed in decoder.isa and unpacked by pimony.cc.

    Both instructions share the upper 32 bits. Keep that half proportional;
    borrow space for dispatch's one-bit c field only from its MAC steps box.
    """
    els.clear()
    half = WORD / 2
    rows = [
        ("pim.dispatch", [("token", 63, 48), ("ASID", 47, 32),
                          ("c", 31, 31), ("MAC steps", 30, 0)],
         [0, half / 2, half, half + MINW, WORD]),
        ("pim.gemv", [("token", 63, 48), ("ASID", 47, 32),
                      ("outputs", 31, 16), ("steps per dot", 15, 0)],
         [0, half / 2, half, half * 1.5, WORD]),
    ]
    pitch = BOX_T + BOX_H + 37
    for r, (mnem, fields, xs) in enumerate(rows):
        top = r * pitch
        for (field, msb, lsb), x0, x1 in zip(fields, xs, xs[1:]):
            bits = msb - lsb + 1
            rect(x0, top + BOX_T, x1 - x0, BOX_H)
            ctext((x0 + x1) / 2, top + BOX_T + BOX_H / 2, field, 18,
                  box=(x0 + 3, x1 - 3))
            if bits == 1:
                text((x0 + x1) / 2, top, str(msb), 15, align="center")
            else:
                text(x0 + 3, top, str(msb), 15)
                text(x1 - 3, top, str(lsb), 15, align="right")
            ctext((x0 + x1) / 2, top + BOX_T + BOX_H + 13, str(bits), 15,
                  color=GREY)
        ctext(COL + text_w(mnem, MNEM) / 2, top + BOX_T + BOX_H / 2,
              mnem, MNEM)
    # Same spacer as the encodings: identical total width at \\textwidth.
    _base("rectangle", COL, BOX_T, COL_W, BOX_H, strokeColor="transparent")
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pim_payload.excalidraw")
    json.dump({"type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
               "elements": els, "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
               "files": {}}, open(out, "w"), indent=1)
    print(f"wrote {out}: {len(els)} elements")

for name, (fmt, fields, rows) in FIGS.items():
    figure(name, fmt, fields, rows)
payload_figure()
for w in warnings:
    print("WARN", w)
