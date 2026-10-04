#!/usr/bin/env python3
# Sequencer result write-backs (writes into pimout): how long from "added" to "issued to DRAM"?
# usage: python3 writeback_wait.py <run.log> [pimout_lo pimout_hi]   (default range = bert_pim_g7pt pimout)
import re, sys, statistics
from collections import defaultdict, deque

lo = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x80049D40   # nm bert_pim_g7pt: pimout
hi = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x8004B540
ansi = re.compile(r"\x1b\[[0-9;]*m")
pat = re.compile(r"\[(AddTransaction\(WR\)|AddCommand|IssueCommand)\] cid: (\d+) clk: (\d+) \| "
                 r"(WRITE|WRITE_PRECHARGE) \|.*?addr: 0X([0-9A-F]+)")

added = defaultdict(deque)          # (cid, addr) -> add clks not yet issued
occ = defaultdict(int)              # cid -> writes in the write buffer (added, no command yet)
occ_max_roi = defaultdict(int)
waits, first, last = [], None, None
for ln in open(sys.argv[1], errors="replace"):
    m = pat.search(ansi.sub("", ln))
    if not m:
        continue
    kind, cid, clk, addr = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(5), 16)
    seq = lo <= addr < hi
    if kind.startswith("AddTransaction"):
        occ[cid] += 1
        if seq:
            added[(cid, addr)].append(clk)
            first = clk if first is None else first
            last = clk
    elif kind == "AddCommand":
        occ[cid] -= 1
    else:  # IssueCommand
        if seq and added[(cid, addr)]:
            waits.append(clk - added[(cid, addr)].popleft())
    if first is not None:
        occ_max_roi[cid] = max(occ_max_roi[cid], occ[cid])

waits.sort()
print(f"write-backs: {len(waits)}   wait added->issued (DRAM clk): "
      f"median {statistics.median(waits):.0f}  mean {statistics.mean(waits):.0f}  "
      f"p90 {waits[int(0.9 * len(waits))]}  max {waits[-1]}  total {sum(waits)}")
print("max writes waiting in a channel's write buffer once write-backs began:",
      dict(sorted(occ_max_roi.items())), "(buffer holds 96)")
