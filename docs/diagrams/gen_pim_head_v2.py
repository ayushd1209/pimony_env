#!/usr/bin/env python3
"""One decode step of an attention head on the PIM, black-line edition (Section 6.6, sec:eval:overlap).

Same rows, steps and arrows as gen_pim_head.py, drawn as a sequence diagram: lane tags with
lifelines, time running down; each of the five steps of the text is one block of CPU rows,
left-aligned like a listing. Provenance by fill (figstyle.py): white = standard,
hatched = PIMony, sand = this work (our instructions).
Traced to code: gpt3.c -DPIM_ATT, pim_att_append() (step 1) and pim_att_head() with
pim_att_stage() and pim_att_issue() (steps 2-5); every clean and invalidate is drawn.
"done" = pimComplete() in pimony.cc raising the completion interrupt that ends pim.wait.
Run: python3 gen_pim_head_v2.py [--svg-only]  ->  pim_head_step_v2.{svg,pdf,png}
"""
import os
from figstyle import *

# ---------------------------------------------------------------- the step, in code order
# (step, CPU row, PIM entry). Markup: `mono` *italic* ~grey~ ^superscript (figstyle.rich).
# job = PIM busy for this row and the next, done wakes that pim.wait; data = result, read by the next row.
ROWS = [
    (1, "store new *k*, *v*",         None),                             # pim_att_append()
    (0, "`pim.fence` ~(clean)~",      None),
    (2, "store *q*",                  None),                             # pim_att_stage(q)
    (0, "`pim.fence` ~(clean)~",      None),
    (0, "store descriptor",           None),                             # pim_att_issue(0)
    (0, "`pim.fence` ~(clean)~",      None),
    (0, "`pim.gemv`",                 ("job", "*K* × *q*")),
    (0, "`pim.wait` ~(sleeps)~",      None),
    (0, "`pim.fence` ~(invalidate)~", ("data", "scores ~in DRAM~")),     # pim_inv_range(attout)
    (0, "load scores",                None),
    (3, "softmax",                    None),
    (4, "store weights",              None),                             # pim_att_stage(weights)
    (0, "`pim.fence` ~(clean)~",      None),
    (5, "store descriptor",           None),                             # pim_att_issue(1)
    (0, "`pim.fence` ~(clean)~",      None),
    (0, "`pim.gemv`",                 ("job", "*V*^T × weights")),
    (0, "`pim.wait` ~(sleeps)~",      None),
    (0, "`pim.fence` ~(invalidate)~", ("data", "output ~in DRAM~")),
    (0, "load output",                None),
]

# ---------------------------------------------------------------- layout
SX = 24                                # step markers
CL, CR = 46, 278                       # CPU rows
PL, PR = 392, 536                      # PIM entries
TAG_Y, TOP = 13, 48                    # lane tags, first row
ROW, GAP = 27, 14                      # row height, extra space between steps
INSET = 2.5                            # PIM entries sit inside their rows
CPU_X, PIM_X = (CL + CR) / 2, (PL + PR) / 2

ys, y = [], TOP
for i, (n, _, _) in enumerate(ROWS):
    if n and i:
        y += GAP
    ys.append(y)
    y += ROW
BOT = ys[-1] + ROW

# ---- lanes
busy = [(ys[i] + INSET, ys[i + (p[0] == "job")] + ROW - INSET) for i, (_, _, p) in enumerate(ROWS) if p]
for x, name, skip in ((CPU_X, "CPU", ()), (PIM_X, "PIM", busy)):
    lifeline(x, TAG_Y + 12.5, BOT + 10, skip)
    nameplate(x, TAG_Y, "", name)

# ---- CPU: one block per step
steps = []
for i, (n, _, _) in enumerate(ROWS):
    if n:
        steps.append([i, i])
    else:
        steps[-1][1] = i
for a, b in steps:
    for i in range(a, b + 1):
        rect(CL, ys[i], CR - CL, ROW, NEW if ROWS[i][1].startswith("`pim.") else WHITE, 0)
    for i in range(a + 1, b + 1):
        line(CL, ys[i], CR, ys[i], 0.6)
    rect(CL, ys[a], CR - CL, ys[b] + ROW - ys[a], "none", SW)
    badge(SX, ys[a] + ROW / 2, ROWS[a][0])
for i, (_, s, _) in enumerate(ROWS):
    rich(CL + 12, ys[i] + ROW / 2, s, 15, anchor="start", box=(CL + 4, CR - 4))

# ---- PIM
XM = (CR + PL) / 2                                                        # elbow of the result arrows
for i, (_, _, pim) in enumerate(ROWS):
    if not pim:
        continue
    kind, label = pim
    nxt, cy, ny = ROWS[i + 1][1], ys[i] + ROW / 2, ys[i + 1] + ROW / 2  # ny: the CPU row it ends at
    if kind == "job":
        assert nxt.startswith("`pim.wait"), "a job ends at its pim.wait"
        top, bot = ys[i] + INSET, ys[i + 1] + ROW - INSET
        box(PL, top, PR - PL, bot - top, "pim")
        rich(PIM_X, (top + bot) / 2, label, 16, wt=500, plate=True, box=(PL + 4, PR - 4))
        wire([(CR, cy), (PL, cy)])                                        # pim.gemv starts the job
        wire([(PL, ny), (CR, ny)])                                        # completion wakes pim.wait
        ctext(XM, ny - 9, "done", 13.5, "sans", 400, SUB)
    else:
        assert nxt.startswith("load"), "the result is read by a load"
        box(PL, ys[i] + INSET, PR - PL, ROW - 2 * INSET, "std")
        rich(PIM_X, cy, label, 14.5, box=(PL + 4, PR - 4))
        wire([(PL, cy), (XM, cy), (XM, ny), (CR, ny)])                    # the next load reads it

LY = BOT + 30
legend((SX - 11 + PR) / 2, LY, [("std", "Standard"), ("pim", "PIMony"), ("new", "This work")])
save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "pim_head_step_v2"),
     3, -2, PR + 10 - 3, LY + 16 + 10 + 2, "attention head on the PIM")
