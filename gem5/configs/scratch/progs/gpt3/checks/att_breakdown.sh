#!/bin/bash
# -DATT_BREAKDOWN read-out (gpt3.c, -DPIM_ATT_N=32): one stats block per part of PIM heads 8-11.
# Cycles = simTicks/333. Prints every block, the per-part average over heads 8-11 minus the marker
# cost (each head's empty block), the parts grouped, and checks against the unmarked run's section 2.
# Block map = the ATT_BK markers in gpt3.c: 76 blocks, section 2 = blocks 2-68. Each append fence.cl
# block also holds the loop step to the next head; each read-back ctx block the call into the next head.
# Usage: bash ~/pimony_env/gem5/configs/scratch/progs/gpt3/checks/att_breakdown.sh [run] [reference]
R=${1:-gpt3_hs2_n32_bk_o3}; F=${2:-gpt3_hs2_n32_o3}
cd ~/pimony_env/gem5/m5out || exit 1
for d in "$R" "$F"; do [ -s "$d/stats.txt" ] || { echo "no $d/stats.txt"; exit 1; }; done
awk -v run="$R" -v ref="$F" '
function A(h)     { return 3 + 2 * (h - 8) }                  # append copy; fence.cl = A(h) + 1
function H(h)     { return 12 + 14 * (h - 8) }                # empty block; parts follow
function pb(p, h) { return p == 1 ? A(h) : p == 2 ? A(h) + 1 : H(h) + p - 2 }
function cp(p, h) { return t[pb(p, h)] / C - mk[h] }          # part minus marker cost
function grp(list, h,   k, s, a) { split(list, a, " "); s = 0; for (k in a) s += cp(a[k], h); return s }
FNR == 1 { f++ }
/Begin Simulation Statistics/ { if (f == 1) rb++; else b++; next }
f == 1 { if (rb == 2 && $1 == "simTicks") rt = $2; else if (rb == 2 && $1 == "system.cpu.quiesceCycles") rq = $2; next }
$1 == "simTicks" { t[b] = $2 }
$1 == "system.cpu.quiesceCycles" { q[b] = $2 }
$1 == "system.cpu.commitStats0.numInsts" { n[b] = $2 }
END {
  C = 333; NB = 76
  if (b != NB) { printf "%s: %d stats blocks, expected %d (run not finished, or not this block map)\n", run, b, NB; exit 1 }
  split("append K/V copy|append K/V fence.cl|stage q copy|stage q fence.cl|issue 0 (desc+fence.cl+gemv)|" \
        "pim.wait 0|fence.inv scores|read-back + scale scores|softmax|stage weights copy|" \
        "stage weights fence.cl|issue 1 (desc+fence.cl+gemv)|pim.wait 1|fence.inv ctx|read-back ctx", PN, "|")
  nm[1] = "s1 LN 1 + QKV"; nm[2] = "s2 append, heads 0-7"; nm[11] = "s2 append heads 12-31, PIM heads 0-7"
  nm[68] = "s2 PIM heads 12-31"
  for (h = 8; h < 12; h++) { mk[h] = t[H(h)] / C; nm[H(h)] = "h" h " marker only (empty block)"
                             for (p = 1; p <= 15; p++) nm[pb(p, h)] = "h" h " " PN[p] }
  split("s3 Wo|s4 residual 1 + LN 2|s5 W1|s6 GELU|s7 W2|s8 residual 2|ROI end dump|m5_exit dump (from blk 75)", T, "|")
  for (k = 1; k <= 8; k++) nm[68 + k] = T[k]

  printf "%s, cycles = simTicks/333\n%4s  %-40s %10s %9s %9s\n", run, "blk", "part", "cycles", "quiesce", "insts"
  for (k = 1; k <= NB; k++) printf "%4d  %-40s %10d %9d %9d\n", k, nm[k], t[k] / C, q[k], n[k]

  printf "\nPer part, PIM heads 8-11: cycles minus that head'"'"'s marker cost\n%-30s %8s %8s %8s %8s %9s\n", "part", "h8", "h9", "h10", "h11", "avg"
  for (p = 1; p <= 15; p++) { s = 0; printf "%-30s", PN[p]
    for (h = 8; h < 12; h++) { v = cp(p, h); s += v; tot[h] += v; printf " %8.0f", v }
    printf " %9.1f\n", s / 4 }
  s = 0; printf "%-30s", "sum of the 15 parts"; for (h = 8; h < 12; h++) { s += tot[h]; printf " %8.0f", tot[h] }; printf " %9.1f\n", s / 4
  avgtot = s / 4

  printf "\nGrouped per head (marker cost subtracted)\n%-30s %8s %8s %8s %8s %9s\n", "group", "h8", "h9", "h10", "h11", "avg"
  split("plain copies|fence instructions|dispatch (both issues)|wait without sleep|sleep (quiesceCycles)|softmax|total", GN, "|")
  for (g = 1; g <= 7; g++) { s = 0; printf "%-30s", GN[g]
    for (h = 8; h < 12; h++) {
      sl = q[pb(6, h)] + q[pb(13, h)]
      v = g == 1 ? grp("1 3 8 10 15", h) : g == 2 ? grp("2 4 7 11 14", h) : g == 3 ? grp("5 12", h) : \
          g == 4 ? grp("6 13", h) - sl : g == 5 ? sl : g == 6 ? cp(9, h) : tot[h]
      s += v; printf " %8.0f", v }
    printf " %9.1f\n", s / 4 }
  print "copies = append, stage q, stage weights, both read-backs; fences = the 3 fence.cl parts + the 2 fence.inv parts"

  printf "\nChecks (reference %s, section 2)\n", ref
  ms = 0; for (h = 8; h < 12; h++) ms += mk[h]; ms /= 4
  printf "marker cost per block (empty blocks h8-h11): %.0f %.0f %.0f %.0f cycles, avg %.1f; insts %d %d %d %d; quiesce %d %d %d %d\n", \
         mk[8], mk[9], mk[10], mk[11], ms, n[H(8)], n[H(9)], n[H(10)], n[H(11)], q[H(8)], q[H(9)], q[H(10)], q[H(11)]
  ref2 = rt / C
  printf "parts per head: %.0f vs c_P + p = %.0f / 32 = %.1f (c_P %.1f + p %.1f): %+.0f (%+.1f%%)\n", \
         avgtot, ref2, ref2 / 32, (ref2 - rq) / 32, rq / 32, avgtot - ref2 / 32, 100 * (avgtot / (ref2 / 32) - 1)
  s2 = 0; sq = 0; for (k = 2; k <= 68; k++) { s2 += t[k] / C; sq += q[k] }
  printf "section 2, blocks 2-68: %.0f vs %.0f: %+.0f (%+.2f%%); minus 66 x marker cost: %.0f (%+.2f%%)\n", \
         s2, ref2, s2 - ref2, 100 * (s2 / ref2 - 1), s2 - 66 * ms, 100 * ((s2 - 66 * ms) / ref2 - 1)
  oq = 0; for (h = 8; h < 12; h++) for (p = 1; p <= 15; p++) if (p != 6 && p != 13) oq += q[pb(p, h)]
  printf "sleep, blocks 2-68: %d vs %d: %+d; heads 8-11 outside the two waits: %d\n", sq, rq, sq - rq, oq
  printf "unmarked heads of this run, blocks (2 + 11 + 68) / 28: %.0f per head\n", (t[2] + t[11] + t[68]) / C / 28
}' "$F/stats.txt" "$R/stats.txt"
