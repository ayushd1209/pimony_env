#!/bin/bash
# Step 5 report, attention-head-split batch (2026-10-19). Per run, from simTicks (cycles = ticks/333.33),
# over the 8 layer sections: section 2, whole layer, matmul (1,3,5,7), CPU asleep (layer / section 2),
# waits that slept, CPU L2 demand misses, average L2 miss latency (all L2 demand misses, cycles).
# Then the matmul-only speedups and the fetch/inst gate on the matmul sections.
# Usage: bash ~/pimony_env/gem5/configs/scratch/progs/gpt3/checks/hs_report.sh
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
for d in gpt3_hs_n0_o3 gpt3_hs_n28_o3 gpt3_hs_n32_o3 gpt3_hs_mm_o3 gpt3_hs_att_o3 gpt3_scr0_o3 gpt3_blas_scrub_o3 \
         gpt3_pt_o3_f2 gpt3_blas_c_o3; do row $d; done
echo
B0=$(mm gpt3_blas_c_o3); P0=$(mm gpt3_pt_o3_f2); B1=$(mm gpt3_blas_scrub_o3); P1=$(mm gpt3_scr0_o3); P2=$(mm gpt3_hs_n0_o3)
awk -v b0=$B0 -v p0=$P0 -v b1=$B1 -v p1=$P1 -v p2=$P2 'BEGIN {
  printf "matmul-only speedup, old  (gpt3_blas_c / gpt3_pt, no scrub):     %d / %d = %.2fx\n", b0, p0, b0/p0
  printf "matmul-only speedup, new  (gpt3_blas_scrub / gpt3_scr0, scrub):  %d / %d = %.2fx\n", b1, p1, b1/p1
  printf "  cross-check             (gpt3_blas_scrub / gpt3_hs_n0):        %d / %d = %.2fx\n", b1, p2, b1/p2 }'
echo
echo "fetch/inst gate on the matmul sections (compare runs to each other, not to 1.0):"
for d in gpt3_pt_o3_f2 gpt3_scr0_o3 gpt3_hs_n0_o3 gpt3_blas_c_o3 gpt3_blas_scrub_o3; do
  awk -v d=$d '/Begin Simulation Statistics/{b++} /^system\.cpu\.icache\.demandAccesses::total/{a[b]=$2}
       /^system\.cpu\.commitStats0\.numInsts /{n[b]=$2}
       END { printf "  %-20s s1 %.3f  s3 %.3f  s5 %.3f  s7 %.3f\n", d, a[1]/n[1], a[3]/n[3], a[5]/n[5], a[7]/n[7] }' $d/stats.txt
done
