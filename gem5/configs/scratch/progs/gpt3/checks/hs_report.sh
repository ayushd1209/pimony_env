#!/bin/bash
# Step 5 report, attention-head-split batch. Per run, from simTicks (cycles = ticks/333.33),
# over the 8 layer sections: section 2, whole layer, matmul (1,3,5,7), CPU asleep (layer / section 2),
# waits that slept, CPU L2 demand misses, average L2 miss latency (all L2 demand misses, cycles).
# Then the matmul-only speedups, the fetch/inst gate on the matmul sections, and the L2 miss latency
# of blocks 1+2 for the N=0 runs with and without the overlaps (the 10-03 interference metric).
# Usage: bash ~/pimony_env/gem5/configs/scratch/progs/gpt3/checks/hs_report.sh [prefix]
#   prefix gpt3_hs  = the 2026-10-19 batch (GELU not vectorised, see GAPS caveat 5) -- default
#   prefix gpt3_hs2 = the rerun with the GELU fix
P=${1:-gpt3_hs}
cd ~/pimony_env/gem5/m5out || exit 1
row() {  # dir
  awk -v d="$1" '/Begin Simulation Statistics/{b++} b>=1 && b<=8 {
      if ($1 == "simTicks") t[b] = $2
      else if ($1 == "system.cpu.quiesceCycles") q[b] = $2
      else if ($1 == "system.workload.inst.quiesce") w += $2
      else if ($1 == "system.l2cache.demandMisses::cpu.data") m += $2
      else if ($1 == "system.l2cache.demandMisses::total") mt += $2
      else if ($1 == "system.l2cache.demandMissLatency::total") lt += $2 }
    END { s = 0; sl = 0; for (i = 1; i <= 8; i++) { s += t[i]; sl += q[i] }
      printf "%-20s %9d %9d %9d %8d %7d %5d %9d %6.1f\n", d, t[2]/333.33, s/333.33, (t[1]+t[3]+t[5]+t[7])/333.33,
             sl, q[2], w, m, (mt ? lt/mt/333.33 : 0) }' "$1/stats.txt"
}
mm() { awk '/Begin Simulation Statistics/{b++} b>=1 && b<=8 && $1 == "simTicks" {t[b] = $2}
            END { printf "%d", (t[1]+t[3]+t[5]+t[7])/333.33 }' "$1/stats.txt"; }
printf "%-20s %9s %9s %9s %8s %7s %5s %9s %6s\n" run s2 layer matmul asleep s2sleep waits cpuL2miss L2lat
for d in ${P}_n0_o3 ${P}_n28_o3 ${P}_n32_o3 ${P}_mm_o3 ${P}_att_o3 gpt3_scr0_o3 gpt3_blas_scrub_o3 \
         gpt3_pt_o3_f2 gpt3_blas_c_o3; do row $d; done
echo
B0=$(mm gpt3_blas_c_o3); P0=$(mm gpt3_pt_o3_f2); B1=$(mm gpt3_blas_scrub_o3); P1=$(mm gpt3_scr0_o3); P2=$(mm ${P}_n0_o3)
awk -v b0=$B0 -v p0=$P0 -v b1=$B1 -v p1=$P1 -v p2=$P2 -v n0=${P}_n0 'BEGIN {
  printf "matmul-only speedup, old  (gpt3_blas_c / gpt3_pt, no scrub):     %d / %d = %.2fx\n", b0, p0, b0/p0
  printf "matmul-only speedup, new  (gpt3_blas_scrub / gpt3_scr0, scrub):  %d / %d = %.2fx\n", b1, p1, b1/p1
  printf "  cross-check             (gpt3_blas_scrub / %s):%*s%d / %d = %.2fx\n", n0, 18 - length(n0), "", b1, p2, b1/p2 }'
echo
echo "fetch/inst gate on the matmul sections (compare runs to each other, not to 1.0):"
for d in gpt3_pt_o3_f2 gpt3_scr0_o3 ${P}_n0_o3 gpt3_blas_c_o3 gpt3_blas_scrub_o3; do
  awk -v d=$d '/Begin Simulation Statistics/{b++} /^system\.cpu\.icache\.demandAccesses::total/{a[b]=$2}
       /^system\.cpu\.commitStats0\.numInsts /{n[b]=$2}
       END { printf "  %-20s s1 %.3f  s3 %.3f  s5 %.3f  s7 %.3f\n", d, a[1]/n[1], a[3]/n[3], a[5]/n[5], a[7]/n[7] }' $d/stats.txt
done
echo
echo "L2 miss latency, blocks 1+2, N=0 (10-03 metric: demandMissLatency/demandMisses, ticks; % vs the no-overlap run):"
lat() {  # dir label base_all base_cpu
  awk -v d="$1" -v l="$2" -v ba="$3" -v bc="$4" '/Begin Simulation Statistics/{b++} b == 1 || b == 2 {
      if ($1 == "system.l2cache.demandMissLatency::total") la += $2
      else if ($1 == "system.l2cache.demandMisses::total") ma += $2
      else if ($1 == "system.l2cache.demandMissLatency::cpu.data") lc += $2
      else if ($1 == "system.l2cache.demandMisses::cpu.data") mc += $2 }
    END { a = la/ma; c = lc/mc; pa = ba ? 100*(a/ba-1) : 0; pc = bc ? 100*(c/bc-1) : 0
      if (pa > -0.05 && pa < 0.05) pa = 0; if (pc > -0.05 && pc < 0.05) pc = 0     # no "-0.0%"
      printf "  %-22s %-34s all misses %7.0f (%+5.1f%%)   CPU loads only %7.0f (%+5.1f%%)\n", d, l, a, pa, c, pc }' "$1/stats.txt"
}
base() { awk '/Begin Simulation Statistics/{b++} b == 1 || b == 2 {
      if ($1 == "system.l2cache.demandMissLatency::total") la += $2
      else if ($1 == "system.l2cache.demandMisses::total") ma += $2
      else if ($1 == "system.l2cache.demandMissLatency::cpu.data") lc += $2
      else if ($1 == "system.l2cache.demandMisses::cpu.data") mc += $2 }
    END { printf "%f %f", la/ma, lc/mc }' "$1/stats.txt"; }
set -- $(base ${P}_n0_o3)
lat ${P}_n0_o3  "no work in the wait (control)" $1 $2
lat ${P}_mm_o3  "+ Q/K read-back in the wait" $1 $2
lat ${P}_att_o3 "+ scores in Wv's waits" $1 $2
echo "  old binaries (10-03 rerun, no scrub), the source of the +5.1% per miss:"
set -- $(base gpt3_v2_ctl_o3_f2)
lat gpt3_v2_ctl_o3_f2 "no work in the wait (control)" $1 $2
lat gpt3_v2_ov_o3_f2  "+ Q/K read-back in the wait" $1 $2
lat gpt3_att_o3_f2    "+ scores in Wv's waits" $1 $2
