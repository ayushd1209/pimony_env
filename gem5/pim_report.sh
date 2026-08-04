#!/usr/bin/env bash
# PIM e2e log/stats reader. Two modes:
#   ./pim_report.sh table  <m5out_dir> [m5out_dir ...]   # side-by-side stats table
#   ./pim_report.sh phases <run.log>                     # phase timeline from a DRAMsim3 trace
set -u

# stat name -> label. Add rows here as you find stats worth defending.
STATS=(
"simTicks|total ticks"
"simInsts|instructions committed"
"system.cpu.numCycles|CPU cycles"
"system.cpu.cpi|CPI"
"system.cpu.idleCycles|idle cycles (O3 only)"
"system.cpu.quiesceCycles|quiesced cycles (O3 only)"
"system.cpu.commit.commitSquashedInsts|squashed insts (O3 only)"
"system.cpu.dcache.CleanSharedReq.missLatency::total|fence.cl L1 latency (ticks)"
"system.cpu.dcache.InvalidateReq.missLatency::total|fence.inv L1 latency (ticks)"
"system.l2cache.CleanSharedReq.mshrMissLatency::total|fence.cl L2->mem (ticks)"
"system.l2cache.InvalidateReq.mshrMissLatency::total|fence.inv L2->mem (ticks)"
"system.l2bus.transDist::CleanSharedReq|CleanShared pkts past L1"
"system.l2bus.transDist::InvalidateReq|Invalidate pkts past L1"
"system.l2bus.transDist::WriteClean|WriteClean pkts (dirty push-out)"
"system.cpu.dcache.ReadReq.accesses::total|dcache read accesses"
"system.cpu.dcache.ReadReq.misses::total|dcache read misses"
"system.cpu.dcache.WriteReq.accesses::total|dcache write accesses"
"system.cpu.dcache.WriteReq.misses::total|dcache write misses"
"system.l2cache.overallMisses::total|L2 misses"
"system.mem_ctrl.numReads::total|DRAM reads"
"system.mem_ctrl.numWrites::total|DRAM writes"
"system.mem_ctrl.bytesWritten::total|DRAM bytes written"
)

get() { # get <dir> <statname>
  awk -v k="$2" '$1==k {print $2; found=1; exit} END{if(!found) print "-"}' "$1/stats.txt" 2>/dev/null || echo "-"
}

do_table() {
  dirs=("$@")
  printf "%-38s" "stat"
  for d in "${dirs[@]}"; do printf "%18s" "$(basename "$d")"; done; echo
  printf "%-38s" "--------------------------------------"
  for d in "${dirs[@]}"; do printf "%18s" "-----------------"; done; echo
  for row in "${STATS[@]}"; do
    name="${row%%|*}"; label="${row#*|}"
    printf "%-38s" "$label"
    for d in "${dirs[@]}"; do printf "%18s" "$(get "$d" "$name")"; done; echo
  done
  echo
  echo "exit reason / sanity:"
  for d in "${dirs[@]}"; do
    printf "  %-18s insts=%s ticks=%s\n" "$(basename "$d")" "$(get "$d" simInsts)" "$(get "$d" simTicks)"
  done
}

do_phases() {
  log="$1"
  echo "== gem5-tick events (strip ANSI, drop response chatter) =="
  sed -r 's/\x1B\[[0-9;]*[mK]//g' "$log" \
    | grep -E "^ *[0-9]+: system|PIM (dispatch|FIRE|token)" \
    | grep -vE "Attempting to send|responses outstanding|Queuing response" \
    | awk '{ t=$1; sub(":","",t);
             if (prev!="") printf "  +%-9d %s\n", t-prev, $0;
             else printf "  %-10s %s\n", "start", $0;
             prev=t }'
  echo
  echo "== DRAM/PIM transaction order (the correctness proof) =="
  sed -r 's/\x1B\[[0-9;]*[mK]//g' "$log" \
    | grep -E "AddTransaction|IssueCommand.*(MAC|MACINTR|WRITE)" \
    | sed 's/^ *//'
  echo
  echo "== exit =="
  grep -E "Exiting @|Assertion|abort" "$log" | tail -3
}

case "${1:-}" in
  table)  shift; do_table "$@" ;;
  phases) shift; do_phases "$1" ;;
  *) echo "usage: $0 table <dir>... | $0 phases <log>"; exit 1 ;;
esac
