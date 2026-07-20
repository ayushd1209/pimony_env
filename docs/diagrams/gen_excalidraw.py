#!/usr/bin/env python3
"""Generate an accurate Excalidraw diagram of the PIMony memory stack."""
import json, itertools, os

_seed = itertools.count(1000)
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

# Palette
CPU_BG="#a5d8ff"; CPU_ST="#1971c2"
FE_BG="#fff3bf"; FE_ST="#e8590c"          # memory controller front-end (emphasis)
SUB_BG="#ffffff"; SUB_ST="#495057"
DATA_BG="#ffe3e3"; DATA_ST="#e03131"
WRAP_BG="#f1f3f5"; WRAP_ST="#868e96"
ETOP_BG="#d3f9d8"; ETOP_ST="#2f9e44"
ELOW_BG="#c3fae8"; ELOW_ST="#0ca678"
DRAM_BG="#eebefa"; DRAM_ST="#9c36b5"
INT_ST="#f08c00"

# ======================= 1. CPU =======================
rect(360, 20, 980, 150, CPU_ST, CPU_BG, "solid", 2)
text(380, 32, "CPU  —  RISC-V O3 / timing core", 20, CPU_ST, 1)
text(380, 66,
     "pim.dispatch rd, rs1(addr), rs2(size)   |   pim.wait rd(token)",
     14, "#1e1e1e", 3)
text(380, 90,
     "src/arch/riscv/isa/decoder.isa (~2191)  builds a Request",
     13, "#495057", 3)
text(380, 110,
     "mem_flags = [PIM_DISPATCH (0x08000000), UNCACHEABLE]",
     13, "#495057", 3)
text(380, 130,
     "store payload  =  [63:48] token  |  [47:0] size_bytes",
     13, "#1971c2", 3)

# CPU -> controller
arrow([(850,170),(850,250)], "#1e1e1e", 2)
text(862, 188, "gem5 Packet  (timing ResponsePort protocol)", 13, "#1e1e1e", 3)
text(862, 206, "one cache-line burst == system cache-line size", 11, "#868e96", 3)

# ======= 2. MEMORY CONTROLLER FRONT-END (emphasis) =======
rect(120, 250, 1460, 660, FE_ST, FE_BG, "solid", 3)
text(140, 262, "MEMORY CONTROLLER  —  FRONT-END (bus-facing half)",
     20, FE_ST, 1)
text(140, 290,
     "src/mem/pimony.cc   class gem5::memory::DRAMsim3 : AbstractMemory"
     "   (filename renamed, class still 'DRAMsim3')",
     13, "#495057", 3)

# --- MemoryPort / bus slave ---
rect(150, 330, 300, 110, SUB_ST, SUB_BG, "solid", 1)
text(164, 340, "Bus slave interface", 15, "#1e1e1e", 1)
text(164, 364, "MemoryPort : ResponsePort", 12, "#495057", 3)
text(164, 384, "recvTimingReq() :200", 12, "#495057", 3)
text(164, 402, "recvAtomic / recvFunctional", 12, "#495057", 3)
text(164, 420, "handshakes valid/ready beats", 11, "#868e96", 3)

# --- PIM_DISPATCH intercept (doorbell) ---
rect(150, 460, 300, 150, "#e8590c", "#ffe8cc", "solid", 2)
text(164, 470, "PIM doorbell decode", 15, "#d9480f", 1)
text(164, 494, "if flag PIM_DISPATCH  :220", 12, "#495057", 3)
text(164, 512, "payload = getLE<u64>()", 12, "#495057", 3)
text(164, 530, "size = payload & 0xFFFFFFFFFFFF", 11, "#495057", 3)
text(164, 547, "token = payload >> 48", 12, "#495057", 3)
text(164, 565, "wrapper.enqueuePIM(", 12, "#d9480f", 3)
text(164, 582, "   addr, size, token)", 12, "#d9480f", 3)
text(164, 600, "ACK CPU same tick (async!)", 11, "#e03131", 3)

# --- Admission / flow control ---
rect(470, 330, 300, 110, SUB_ST, SUB_BG, "solid", 1)
text(484, 340, "Admission / flow control", 15, "#1e1e1e", 1)
text(484, 364, "can_accept =", 12, "#495057", 3)
text(484, 382, " nbrOutstanding()", 12, "#495057", 3)
text(484, 400, "   < wrapper.queueSize() :217", 11, "#495057", 3)
text(484, 420, "full -> retryReq, nack+retry", 11, "#e03131", 3)

# --- Transaction table ---
rect(470, 460, 300, 90, SUB_ST, SUB_BG, "solid", 1)
text(484, 470, "Transaction table (MSHR-like)", 14, "#1e1e1e", 1)
text(484, 494, "outstandingReads[addr]  :249", 12, "#495057", 3)
text(484, 512, "outstandingWrites[addr] :261", 12, "#495057", 3)
text(484, 530, "FIFO per-address match", 11, "#868e96", 3)

# --- Posted write buffer ---
rect(470, 570, 300, 90, SUB_ST, SUB_BG, "solid", 1)
text(484, 580, "Posted-write buffer", 14, "#1e1e1e", 1)
text(484, 604, "writes ACK early :266", 12, "#495057", 3)
text(484, 622, "accessAndRespond() :308", 12, "#495057", 3)
text(484, 640, "engine drains in background", 11, "#868e96", 3)

# --- Response channel ---
rect(790, 330, 300, 110, SUB_ST, SUB_BG, "solid", 1)
text(804, 340, "Response channel", 15, "#1e1e1e", 1)
text(804, 364, "responseQueue (deque)", 12, "#495057", 3)
text(804, 382, "sendResponse() :117", 12, "#495057", 3)
text(804, 400, "retryResp backpressure", 12, "#495057", 3)
text(804, 420, "signalDrainDone when empty", 11, "#868e96", 3)

# --- tick co-sim clock ---
rect(790, 460, 300, 90, SUB_ST, SUB_BG, "solid", 1)
text(804, 470, "Co-sim clock", 14, "#1e1e1e", 1)
text(804, 494, "tick() :156  ->  wrapper.tick()", 12, "#495057", 3)
text(804, 512, "reschedule @ +clockPeriod ns", 11, "#495057", 3)
text(804, 530, "TCK pulled from engine GetTCK()", 11, "#868e96", 3)

# --- Backing store (data) ---
rect(790, 570, 300, 90, DATA_ST, DATA_BG, "solid", 1)
text(804, 580, "Backing store  (DATA lives here)", 13, "#c92a2a", 1)
text(804, 604, "AbstractMemory::access(pkt)", 12, "#495057", 3)
text(804, 622, "functionally correct, 0 timing", 11, "#495057", 3)
text(804, 640, "engine never sees the bytes", 11, "#e03131", 3)

# --- Completion handlers (callbacks land here) ---
rect(1110, 330, 450, 330, "#2f9e44", "#ebfbee", "solid", 2)
text(1124, 340, "Completion handlers  (callbacks land here)", 15, "#2b8a3e", 1)

rect(1126, 372, 420, 60, SUB_ST, SUB_BG, "solid", 1)
text(1136, 380, "readComplete(addr) :375   <- read_cb", 12, "#1e1e1e", 3)
text(1136, 398, "pop outstandingReads, real access,", 11, "#495057", 3)
text(1136, 414, "send response packet to CPU", 11, "#495057", 3)

rect(1126, 442, 420, 44, SUB_ST, SUB_BG, "solid", 1)
text(1136, 450, "writeComplete(addr) :401  <- write_cb", 12, "#1e1e1e", 3)
text(1136, 468, "bookkeeping only (acked early)", 11, "#495057", 3)

rect(1126, 496, 420, 156, "#e8590c", "#fff3bf", "solid", 2)
text(1136, 504, "pimComplete(token) :347   <- pim_cb", 13, "#d9480f", 3)
text(1136, 524, "machine->threads[0] -> RiscvISA::ISA", 11, "#495057", 3)
text(1136, 540, "isa->markPimToken(token)  (scoreboard)", 12, "#2b8a3e", 3)
text(1136, 558, "cpu->postInterrupt(tid, pimIntNum, 0)", 12, "#e03131", 3)
text(1136, 578, "==> wakes pim.wait  (NO LONGER", 12, "#d9480f", 3)
text(1136, 595, "    exitSimLoop -- thesis seam done)", 12, "#d9480f", 3)
text(1136, 618, "pim_cb is void(token): per-token id", 11, "#868e96", 3)
text(1136, 634, "threaded engine->wrapper->gem5", 11, "#868e96", 3)

# controller -> wrapper
arrow([(850,910),(850,968)], "#1e1e1e", 2)
text(862, 924, "C++ calls into libdramsim3.so", 13, "#1e1e1e", 3)

# upward callbacks (engine -> completion handlers): drawn later from engine

# ======================= 3. WRAPPER =======================
rect(360, 970, 980, 110, WRAP_ST, WRAP_BG, "solid", 2)
text(380, 980, "DRAMsim3Wrapper  (pimony_wrapper.*)  —  pure simulator shim",
     16, "#495057", 1)
text(380, 1010,
     "GetMemorySystem(mem_config, model_config, log_dir, log_level,"
     " pim_cb, read_cb, write_cb)",
     12, "#495057", 3)
text(380, 1030,
     "enqueue()->AddTransaction   enqueuePIM()->AddMACTransaction(addr,"
     " num_macs, token)   tick()->ClockTick()",
     12, "#495057", 3)
text(380, 1052,
     "holds pimony::MemorySystem*  -- namespace isolation, no hardware"
     " counterpart",
     11, "#868e96", 3)

arrow([(850,1080),(850,1138)], "#1e1e1e", 2)

# ============== 4. ENGINE TOP LAYER ==============
rect(120, 1140, 1460, 250, ETOP_ST, ETOP_BG, "solid", 2)
text(140, 1150, "MODEL OF HARDWARE  —  back-end, engine top layer",
     19, ETOP_ST, 1)
text(140, 1176, "ext/dramsim3/PIMony/src/   namespace pimony"
     "   (compiles into libdramsim3.so)", 13, "#495057", 3)

rect(150, 1206, 330, 170, SUB_ST, SUB_BG, "solid", 1)
text(164, 1214, "MemorySystem", 15, "#1e1e1e", 1)
text(164, 1238, "public API gem5 calls", 12, "#495057", 3)
text(164, 1256, "per-cycle: feed reqs/ch", 12, "#495057", 3)
text(164, 1274, "-> dram->cycle()", 12, "#495057", 3)
text(164, 1292, "-> drain resp queues", 12, "#495057", 3)
text(164, 1310, "-> fire 3 callbacks", 12, "#2b8a3e", 3)
text(164, 1334, "writes split across", 11, "#868e96", 3)
text(164, 1350, "even/odd channel pair", 11, "#868e96", 3)

rect(500, 1206, 330, 170, SUB_ST, SUB_BG, "solid", 1)
text(514, 1214, "Request handler", 15, "#1e1e1e", 1)
text(514, 1238, "TraceRequestHandler", 12, "#495057", 3)
text(514, 1256, "queues normal vs PIM", 12, "#495057", 3)
text(514, 1274, "tracks latency / SLO", 12, "#495057", 3)
text(514, 1292, "is_pim_operation_done()", 12, "#2b8a3e", 3)
text(514, 1316, "decides when pim_cb", 11, "#868e96", 3)
text(514, 1332, "fires (all PIM done)", 11, "#868e96", 3)

rect(850, 1206, 330, 80, SUB_ST, SUB_BG, "solid", 1)
text(864, 1214, "PIM  (extends Dram)", 14, "#1e1e1e", 1)
text(864, 1238, "owns dramsim3::PIMSim", 12, "#495057", 3)
text(864, 1256, "per-ch push/top/pop, BW", 12, "#495057", 3)

rect(850, 1296, 330, 80, SUB_ST, SUB_BG, "solid", 1)
text(864, 1304, "Address  /  LLMGenerator", 14, "#1e1e1e", 1)
text(864, 1328, "addr<->ch/ra/bg/ba/ro/co", 12, "#495057", 3)
text(864, 1346, "built-in GEMV/GPT traces", 12, "#495057", 3)

rect(1200, 1206, 360, 170, "#0ca678", "#e6fcf5", "solid", 1)
text(1214, 1214, "Split: timing vs data", 14, "#0b7285", 1)
text(1214, 1240, "engine sees ONLY", 12, "#495057", 3)
text(1214, 1258, "(addr, is_write) or", 12, "#495057", 3)
text(1214, 1276, "(addr, num_macs)", 12, "#495057", 3)
text(1214, 1300, "MAC units are", 12, "#e03131", 3)
text(1214, 1318, "TIMING-ONLY: charge", 12, "#e03131", 3)
text(1214, 1336, "cycles, no arithmetic", 12, "#e03131", 3)
text(1214, 1356, "on backing-store bytes", 11, "#868e96", 3)

arrow([(850,1390),(850,1448)], "#1e1e1e", 2)

# ============== 5. ENGINE LOW LAYER ==============
rect(120, 1450, 1460, 250, ELOW_ST, ELOW_BG, "solid", 2)
text(140, 1460, "MODEL OF HARDWARE  —  cycle-accurate DRAM+PIM core",
     19, ELOW_ST, 1)
text(140, 1486, "ext/dramsim3/PIMony/PIMSim/   namespace dramsim3"
     "   (DRAMsim3 derivative, JEDEC LPDDR5/5X)", 13, "#495057", 3)

rect(150, 1516, 360, 90, SUB_ST, SUB_BG, "solid", 1)
text(164, 1526, "JedecDRAMSystem", 15, "#1e1e1e", 1)
text(164, 1550, "dram_system.h", 12, "#495057", 3)
text(164, 1568, "fans out to N channels", 12, "#495057", 3)
text(164, 1586, "-> PIMController x N", 12, "#0b7285", 3)

rect(540, 1516, 470, 170, "#0ca678", "#e6fcf5", "solid", 2)
text(554, 1526, "PIMController  (per channel)", 15, "#0b7285", 1)
text(554, 1552, "normal cmd queue:"
     " ACT/RD/WR/PRE/REFRESH", 12, "#495057", 3)
text(554, 1572, "PIM cmd queue:"
     " pim_command_queue.cc", 12, "#495057", 3)
text(554, 1592, "scheduling_policy:"
     " PIM_FIRST / MEM_FIRST", 12, "#495057", 3)
text(554, 1612, "bank_mode: SINGLE / DPSA", 12, "#495057", 3)
text(554, 1632, "refresh engine (tREFI)", 12, "#495057", 3)
text(554, 1656, "FR-FCFS bank-state machine, timing enforce",
     11, "#868e96", 3)

rect(1040, 1516, 520, 170, "#9c36b5", "#f8f0fc", "solid", 2)
text(1054, 1526, "PIM command set"
     "  (PIMSim/src/common.h)", 14, "#862e9c", 1)
text(1054, 1552, "D2GWRITE  dram -> global buffer", 12, "#495057", 3)
text(1054, 1570, "H2GWRITE  host -> gbuf (num_macs)", 12, "#495057", 3)
text(1054, 1588, "COMP      all-bank synchronous", 12, "#495057", 3)
text(1054, 1606, "MAC       async per-bankgroup"
     "  <- dispatch", 12, "#e03131", 3)
text(1054, 1624, "MACINTR   bankgroup coordination", 12, "#495057", 3)
text(1054, 1642, "READRES   read result accumulator", 12, "#495057", 3)
text(1054, 1664, "MacState: num_macs, remaining_macs, addr",
     11, "#868e96", 3)

arrow([(850,1700),(850,1758)], "#1e1e1e", 2)

# ============== 6. DRAM DEVICE ==============
rect(120, 1760, 1460, 240, DRAM_ST, DRAM_BG, "solid", 2)
text(140, 1770, "DRAM DEVICE  +  PIM SILICON  (LPDDR5/5X, from .ini)",
     19, "#862e9c", 1)

# hierarchy chain
hx = 150
for label, sub in [
    ("Channel x4", ""),
    ("Rank", "per cfg"),
    ("Bankgroup x4", ""),
    ("Bank x4", "per bg"),
    ("Row", "~49152"),
    ("Column", "1024 / 16b"),
]:
    rect(hx, 1812, 175, 70, "#9c36b5", "#ffffff", "solid", 1)
    text(hx+12, 1826, label, 13, "#1e1e1e", 1)
    if sub:
        text(hx+12, 1850, sub, 11, "#868e96", 3)
    if hx > 150:
        arrow([(hx-55,1847),(hx,1847)], "#862e9c", 2)
    hx += 230

# MAC units row
rect(150, 1900, 690, 80, "#e8590c", "#ffe8cc", "solid", 2)
text(164, 1910, "PIM MAC units  (compute_mode)", 14, "#d9480f", 1)
text(164, 1934, "ASYNC: one per (rank x bankgroup)"
     " <- async pim.dispatch target", 12, "#495057", 3)
text(164, 1954, "ALL_BANK: 1 per channel (lockstep)", 12, "#495057", 3)

rect(860, 1900, 700, 80, "#9c36b5", "#f3d9fa", "solid", 1)
text(874, 1910, "address_mapping = rorabacobgch", 13, "#862e9c", 1)
text(874, 1934, "consecutive cache lines interleave"
     " across channels/bankgroups", 12, "#495057", 3)
text(874, 1954, "where a dispatch addr lands"
     " == which bankgroup/MAC runs it", 11, "#868e96", 3)

# ============ UPWARD CALLBACK ARROWS (engine -> handlers) ============
# from engine top-left up the right side into completion handlers
arrow([(1500,1140),(1500,1010),(1480,1010),(1480,660)], INT_ST, 2,
      dash="dashed")
text(1505, 1060, "read_cb / write_cb / pim_cb", 12, INT_ST, 3)
text(1505, 1078, "(3 std::function callbacks)", 11, "#868e96", 3)

# ============ INTERRUPT back to CPU (far right) ============
arrow([(1560,496+78),(1640,574),(1640,95),(1340,95)], "#e03131", 3)
text(1582, 360, "postInterrupt", 13, "#e03131", 3, align="left")
text(1582, 378, "+ token in", 12, "#e03131", 3)
text(1582, 396, "scoreboard", 12, "#e03131", 3)
text(1582, 414, "wakes pim.wait", 11, "#868e96", 3)

# ============ TITLE ============
text(120, -60, "PIMony memory stack  —  CPU interface + memory controller"
     " (front-end emphasis)", 26, "#1e1e1e", 1)
text(120, -24, "verified against src/mem/pimony.cc (se-mode branch)"
     "  -- file:line refs in code font", 14, "#868e96", 3)

doc = {
    "type": "excalidraw", "version": 2,
    "source": "https://excalidraw.com",
    "elements": elements,
    "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"},
    "files": {},
}
with open(os.path.join(os.path.dirname(__file__), "PIMony_block_diagram.excalidraw"), "w") as f:
    json.dump(doc, f, indent=2)
print("wrote PIMony_block_diagram.excalidraw with", len(elements), "elements")
