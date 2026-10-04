#!/usr/bin/env python3
# Per PIM engine: did any MAC arrive while the previous MAC there was still running?
# usage: python3 engine_gaps.py <run.log>   (a run whose log keeps the MAC lines)
import re, sys
from collections import defaultdict

ansi = re.compile(r"\x1b\[[0-9;]*m")
pat = re.compile(r"\[(AddCommand|IssueCommand)\] cid: (\d+) clk: (\d+) \| MAC \|"
                 r".*?rank: (\d+) bankgroup: (\d+).*?addr: (0X[0-9A-F]+)")

eng = defaultdict(list)                       # (channel, rank, bankgroup) -> events
for ln in open(sys.argv[1], errors="replace"):
    m = pat.search(ansi.sub("", ln))
    if m:
        eng[m.group(2, 4, 5)].append((m.group(1), int(m.group(3)), m.group(6)))

gaps, restarts, per_eng = [], 0, []
for l in eng.values():
    order, first_add, last_issue, adds = [], {}, {}, defaultdict(int)
    for t, clk, a in l:
        if a not in first_add and t == "AddCommand":
            first_add[a] = clk
            order.append(a)
        if t == "AddCommand":
            adds[a] += 1
        else:
            last_issue[a] = clk
    restarts += sum(n - 1 for n in adds.values())
    per_eng.append(len(order))
    gaps += [first_add[b] - last_issue[a] for a, b in zip(order, order[1:])]

print(f"engines: {len(eng)}   MACs per engine: {min(per_eng)}-{max(per_eng)}   "
      f"paused+resumed: {restarts}")
print(f"smallest gap, previous MAC last started -> next MAC arrives: {min(gaps)} DRAM clk")
print("longest MAC = 64 steps x tCCD_L 4 = 256 DRAM clk")
