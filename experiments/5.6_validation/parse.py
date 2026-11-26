#!/usr/bin/env python3
"""skip: plumbing. Thesis 5.6: the instruction tests (Table A.1) and the BERT layer's PIM commands.

Reads the test summary that run_validation.sh writes, and counts the MAC and READRES commands PIMony
logged for the BERT layer and its pim.wait sleeps.
Usage: python3 experiments/5.6_validation/parse.py [OUT]   OUT = run dir relative to gem5/, default m5out
Prints the numbers and writes validation.csv next to this script.
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from common import M5, total, write_csv  # noqa: E402

SUMMARY = M5 / "validation_1110" / "summary.txt"
BERT = "bert_pim_g7pt_o3_wlog"
ANSI = re.compile(r"\x1b\[[0-9;]*m")
PIM_CMD = re.compile(r"AddTransaction\(PIM\)\] cid: \d+ clk: \d+ \| ([A-Z0-9]+) \|")

# Instruction tests: one row per test, pass/fail per CPU, identical commands across CPUs, the test's check
rows, lines = [], open(SUMMARY).read().splitlines()
head = next(i for i, l in enumerate(lines) if l.startswith("test "))
for l in lines[head + 1:]:
    if l.startswith("ALL ") or not l.strip():
        break
    p = l.split(None, 6)
    rows.append({"test": p[0], "timing": p[1], "minor": p[2], "o3": p[3], "commands": p[4], "launches": p[5],
                 "check": p[6] if len(p) > 6 else ""})
write_csv(HERE / "validation.csv", rows)
print(lines[0])
print(f"{len(rows)} tests x 3 CPUs: {sum(r[c] == 'pass' for r in rows for c in ('timing', 'minor', 'o3'))} pass;"
      f" commands identical across CPUs in {sum(r['commands'] == 'same' for r in rows)} tests")
for r in rows:
    if r["check"]:
        print(f"  {r['test']:10s} {r['check']}")
print(next(l for l in lines if l.startswith("ALL ")))

# BERT layer: commands that reached the PIM (PIMony's transaction log) and pim.wait sleeps
counts = {}
for line in open(M5 / BERT / "run.log", errors="replace"):
    m = PIM_CMD.search(ANSI.sub("", line))
    if m:
        counts[m.group(1)] = counts.get(m.group(1), 0) + 1
print(f"\nBERT layer ({BERT}): MAC {counts.get('MAC', 0):,}, READRES {counts.get('READRES', 0):,},"
      f" pim.wait sleeps {total(BERT, 'system.workload.inst.quiesce')}"
      f"  (expected: 1,536 + 576 = 2,112 MACs, one READRES each, one sleep per pim.gemv = 8)")
