#!/usr/bin/env python3
"""pim.gemv sequencer figure, black-line edition (Section: The Memory-Side Sequencer).

Same blocks, wires and labels as gen_gemv_sequencer.py. Provenance moves from stroke
colour to fill (figstyle.py), so every border is black: white = LPDDR5X / standard,
hatched = PIMony, sand = this work.
Traced to code: pimony.cc (GEMV command register, descriptor fetch, IRQ), memory_system.cc
(sequencer), PIMSim pim_controller.cc + pim_command_queue.cc (channel controller),
LPDDR5X_12Gb_x16_8533_pimony.ini (geometry).
Run: python3 gen_gemv_sequencer_v2.py [--svg-only]  ->  pim_gemv_sequencer_v2.{svg,pdf,png}
"""
import os
from figstyle import *

# ---------------------------------------------------------------- layout
L, R = 50, 840                               # content left / right edge
J = (60, 220)                                # GEMV command register / descriptor fetch
G = (255, 545)                               # command generator
P = (300, 440)                               # issue port
T = (550, 830)                               # engine table
C = (590, 820)                               # completion
GEMV_X, IRQ_X = 140, 705                     # pim.gemv in, completion IRQ out
WAIT_CX = 634                                # pim.wait, just left of the interrupt pin
CA_X = 637                                   # C/A leaves the command queues here

CORE_B = 46
MC_T, SQ_T = 92, 116                         # memory controller, sequencer (plate on its edge)
A_T = SQ_T + 34; A_B = A_T + 76              # control row
B_T = A_B + 60; B_B = B_T + 120              # datapath row
FETCH_T, FETCH_B = B_T + 18, B_T + 76
SQ_B = B_B + 12
CC_T = SQ_B + 62; CC_B = CC_T + 102          # channel controller
Q_T, Q_B = CC_T + 28, CC_T + 90
MC_B = CC_B + 14
DV_T = MC_B + 42; DV_B = DV_T + 128          # device
BG_T = DV_T + 36
LY = DV_B + 30                               # legend

# ---- containers
rect(34, MC_T, R + 16 - 34, MC_B - MC_T, "none", 1.1, dash="5 3.5")
ctext(22, (MC_T + MC_B) / 2, "MEMORY CONTROLLER", 12, "sans", 600, SUB, ls=1.6, rot=-90)

rect(L, 0, R - L, CORE_B, WHITE, SW_BOX)
ctext((GEMV_X + 55 + WAIT_CX - 55) / 2, CORE_B / 2, "RISC-V hart", 16.5, "sans", 600)
for cx, mnem in ((GEMV_X, "pim.gemv"), (WAIT_CX, "pim.wait")):
    rect(cx - 55, 9, 110, 28, NEW, SW_BOX)
    ctext(cx, 23, mnem, 15, "mono", 500, box=(cx - 51, cx + 51))
rect(IRQ_X - 6, CORE_B - 12, 12, 12, WHITE, SW)                           # interrupt pin, as Fig. 5.3

rect(L, SQ_T, R - L, SQ_B - SQ_T, WASH, SW_SEQ)
nameplate((L + R) / 2, SQ_T, "pim.gemv", " sequencer")

stack(L, CC_T, R - L, CC_B - CC_T)
rect(L, CC_T, R - L, CC_B - CC_T, WHITE, SW_BOX)
title2(560, CC_T + 14, "Channel controller", " (×4 channels)")

stack(L, DV_T, R - L, DV_B - DV_T)
rect(L, DV_T, R - L, DV_B - DV_T, WHITE, SW_BOX)
title2(66, DV_T + 16, "LPDDR5X-PIM device", " (×4 channels)", anchor="start")

# ---- sequencer (this work)
block(J[0], A_T, J[1] - J[0], A_B - A_T, "GEMV command\nregister", ["one GEMV at a time"], "new")
block(J[0], FETCH_T, J[1] - J[0], FETCH_B - FETCH_T, "Descriptor fetch", kind="new")
block(G[0], A_T, G[1] - G[0], A_B - A_T, "Command generator", kind="new")
block(C[0], A_T, C[1] - C[0], A_B - A_T, "Completion", ["reported once"], "new")
block(P[0], FETCH_T, P[1] - P[0], B_B - FETCH_T, "Issue port", kind="new")

# engine table: one row per engine
BAND, HDR = 24, 22
cols = [("engine", 52), ("busy", 50), ("readout owed", 94), ("write owed", 84)]
xs = [T[0]]
for _, w in cols:
    xs.append(xs[-1] + w)
assert abs(xs[-1] - T[1]) < 1, xs
rh = (B_B - B_T - BAND - HDR) / 4
rect(T[0], B_T, T[1] - T[0], B_B - B_T, WHITE, 0)
rect(T[0], B_T, T[1] - T[0], BAND, NEW, 0)
ctext((T[0] + T[1]) / 2, B_T + BAND / 2, "Engine table", 15, "sans", 500, box=(T[0] + 4, T[1] - 4))
hy = B_T + BAND
line(T[0], hy, T[1], hy, SW)
line(T[0], hy + HDR, T[1], hy + HDR, 0.8)
for i in range(1, 4):
    line(T[0], hy + HDR + i * rh, T[1], hy + HDR + i * rh, 0.5)
for xx in xs[1:-1]:
    line(xx, hy, xx, B_B, 0.5)
for (name, _), a, b in zip(cols, xs, xs[1:]):
    ctext((a + b) / 2, hy + HDR / 2, name, 13, "sans", 500, box=(a + 2, b - 2))
for i, idx in enumerate(["0", "1", None, "31"]):
    cy = hy + HDR + rh * i + rh / 2
    if idx:
        ctext((xs[0] + xs[1]) / 2, cy, idx, 13, "mono", 500, SUB)
    else:
        for a, b in zip(xs, xs[1:]):
            vdots((a + b) / 2, cy)
rect(T[0], B_T, T[1] - T[0], B_B - B_T, "none", SW)

# internal wiring
wire([(J[1], A_T + 38), (G[0], A_T + 38)])                                # command register -> generator
wire([(GEMV_X, A_B), (GEMV_X, FETCH_T)], start=True)                      # register <-> fetch
wire([(G[1], A_T + 38), (C[0], A_T + 38)])                                # all issued
wire([(IRQ_X, B_T), (IRQ_X, A_B)])                                        # nothing owed

D2G_X, MAC_X = 330, 410                                                   # generator -> port
STEP_Y, GATE_Y = A_B + 19, A_B + 48
wire([(D2G_X, A_B), (D2G_X, FETCH_T)])
badge(D2G_X, STEP_Y, 2, WASH)
pc, _ = ctext(D2G_X - 16, STEP_Y, "(per channel)", 13.5, "sans", 400, SUB, "end")   # one per channel
ctext(pc - 5, STEP_Y, "D2GWRITE", 13.5, "mono", 500, INK, "end", box=(GEMV_X + 12, pc))
wire([(MAC_X, A_B), (MAC_X, GATE_Y - 10)], end=False)
hw = gate(MAC_X, GATE_Y, "free?")                                         # busy gate
wire([(MAC_X, GATE_Y + 10), (MAC_X, FETCH_T)])
badge(MAC_X, STEP_Y, 3, WASH)
ctext(MAC_X + 16, STEP_Y, "MAC", 13.5, "mono", 500, INK, "start")
BUSY_X = (xs[1] + xs[2]) / 2                                              # busy column
wire([(BUSY_X, B_T), (BUSY_X, GATE_Y), (MAC_X + hw, GATE_Y)])             # busy -> gate
ctext(530, GATE_Y - 11, "busy", 13.5, "sans", 400, SUB, "end")

for yy, n, mnem in ((B_T + 60, 4, "READRES"), (B_T + 98, 5, "WRITE")):   # owed work -> port
    wire([(T[0], yy), (P[1], yy)])
    badge(P[1] + 28, yy, n, WASH)
    text(P[1] + 42, yy - 6, mnem, 13.5, "mono", 500, box=(P[1], T[0]))

# ---- channel controller (one per channel)
# PIM requests share the read queue (pim_controller.cc:369); PIMony's addition
# is the PIM command queue (pim_command_queue.cc GetQueue)
block(70, Q_T, 218, Q_B - Q_T, "Read queue", ["reads + PIM requests"], ts=14.5)
block(300, Q_T, 140, Q_B - Q_T, "Write queue", ts=14.5)
block(455, Q_T, 90, Q_B - Q_T, "Scheduler", ts=14.5)
rect(557, Q_T, 188, Q_B - Q_T, WHITE, SW)
ctext(651, Q_T + 11.5, "Command queues", 13.5, "sans", 500, box=(561, 741))
block(565, Q_T + 23, CA_X - 4 - 565, Q_B - 5 - Q_T - 23, "per bank", ts=13)
block(CA_X + 4, Q_T + 23, 737 - CA_X - 4, Q_B - 5 - Q_T - 23, "PIM queue", kind="pim", ts=13)
block(755, Q_T, 75, Q_B - Q_T, "Return\nqueue", ts=14.5)

# ---- LPDDR5X-PIM device (one per channel)
for i in range(4):                                                        # one rank shown
    bx = 66 + i * 86
    rect(bx, BG_T, 80, 66, WHITE, SW)
    ctext(bx + 40, BG_T + 11, f"BG{i}", 13, "sans", 500)
    for k in range(4):
        rect(bx + 6 + k * 17.5, BG_T + 22, 15, 22, WHITE, 0.8)            # bank
        box(bx + 6 + k * 17.5, BG_T + 47, 15, 11, "pim", 0.8)             # PU
ctext(66, DV_B - 14, "engine = one bankgroup: 4 banks + 4 PUs (32 in total)", 13, "sans", 400, SUB,
      "start", box=(66, 510))
block(418, BG_T, 84, 66, "Global\nbuffer", kind="pim", ts=14.5)
rect(518, BG_T, 306, 84, WHITE, SW)
ctext(724, BG_T + 13, "Control logic", 14.5, "sans", 500)
block(530, BG_T + 30, CA_X - 5 - 530, 44, "CMD decoder", ts=13)
block(CA_X + 5, BG_T + 30, 812 - CA_X - 5, 44, "MAC control", kind="pim", ts=13)

# ---- wires between units
wire([(GEMV_X, FETCH_B), (GEMV_X, Q_T)], start=True)                      # descriptor read
badge(GEMV_X, SQ_B + 17, 1)
ctext(GEMV_X + 16, SQ_B + 17, "descriptor read", 13.5, "sans", 400, SUB, "start")
OUT_X, J_Y = (P[0] + P[1]) / 2, SQ_B + 34                                 # commands down
wire([(OUT_X, B_B), (OUT_X, Q_T)])                                        # WRITE -> write queue
wire([(OUT_X, J_Y), (250, J_Y), (250, Q_T)])                              # PIM -> read queue
dot(OUT_X, J_Y)
RESP_X = 792                                                              # return queue centre
wire([(RESP_X, Q_T), (RESP_X, B_B)])                                      # responses up
ctext(RESP_X - 10, SQ_B + 17, "responses", 13.5, "sans", 400, SUB, "end")
wire([(GEMV_X, 37), (GEMV_X, A_T)])                                       # pim.gemv in
ctext(GEMV_X + 12, CORE_B + 23, "descriptor address, shape", 13.5, "sans", 400, SUB, "start")
wire([(IRQ_X, A_T), (IRQ_X, CORE_B)])                                     # IRQ to the pin, not pim.wait
badge(IRQ_X, CORE_B + 24, 6)                                              # below the arrowhead
ctext(IRQ_X + 16, CORE_B + 24, "completion IRQ", 13.5, "sans", 400, SUB, "start")
wire([(CA_X, Q_B), (CA_X, BG_T)], start=True)                             # C/A + DQ
ctext(CA_X + 10, MC_B + 17, "C/A, DQ", 13.5, "sans", 400, SUB, "start")

legend((L + R) / 2, LY, [("std", "LPDDR5X / standard"), ("pim", "PIMony"), ("new", "This work")])
save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "pim_gemv_sequencer_v2"),
     6, -10, 862, LY + 16 + 10 + 10, "pim.gemv sequencer")
