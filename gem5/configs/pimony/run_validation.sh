#!/bin/bash
# skip: plumbing. All instruction tests on TimingSimpleCPU, MinorCPU and O3CPU, then the checks.
# Run inside gem5-container, from the gem5 directory:
#   docker exec -w /home/pimony/gem5 gem5-container bash configs/pimony/run_validation.sh
# $1 = gem5 binary (default build/RISCV/gem5.opt). Env: OUT, JOBS, LIMIT (s per run), ONLY="asid wrongpath".
# Per run: $OUT/<test>_<cpu>/{run.log,dbg.log,exit}. Summary: $OUT/summary.txt
set -u
G5=${1:-build/RISCV/gem5.opt}
OUT=${OUT:-m5out/validation}
JOBS=${JOBS:-14}
LIMIT=${LIMIT:-1800}
ONLY=${ONLY:-}
TP=tests/test-progs
CPUS="timing minor o3"
# test|config|binary (empty = the config's own)
TESTS="asid|configs/pimony/fs_pim_test.py|$TP/pim_baremetal/pim_baremetal
csr_priv|configs/pimony/fs_pim_test.py|$TP/csr_priv/csr_priv
e2e|configs/pimony/fs_fence_e2e.py|
fence_cbo|configs/pimony/fs_fence_test.py|
gemv|configs/pimony/fs_pim_gemv.py|$TP/pim_gemv/pim_gemv_ladder
group|configs/pimony/fs_pim_group.py|
issue|configs/pimony/fs_pim_issue.py|$TP/pim_issue/iss_16
order_f|configs/pimony/fs_fence_order.py|$TP/fence_order/fence_order_fence
order_nf|configs/pimony/fs_fence_order.py|$TP/fence_order/fence_order_nofence
sweep|configs/pimony/fs_pim_sweep.py|
wrongpath|configs/pimony/fs_pim_test.py|$TP/pim_wrongpath/pim_wrongpath"

picked() { [ -z "$ONLY" ] || echo " $ONLY " | grep -q " $1 "; }

run_one() {                                    # $1 = test|config|binary|cpu
    IFS='|' read -r t cfg bin c <<< "$1"
    local d=$OUT/${t}_$c flags=DRAMsim3
    rm -rf "$d"; mkdir -p "$d"
    if [ "$c" = o3 ] && { [ "$t" = wrongpath ] || [ "$t" = sweep ]; }; then
        flags=DRAMsim3,LSQUnit                 # prints "PIM token N returned by squashed"
    fi
    timeout -k 10 "$LIMIT" "$G5" -d "$d" --debug-flags=$flags --debug-file=dbg.log \
        "$cfg" "$c" $bin > "$d/run.log" 2>&1
    echo $? > "$d/exit"
}
export -f run_one
export G5 OUT LIMIT

mkdir -p "$OUT"
jobs=()
while IFS= read -r line; do
    picked "${line%%|*}" || continue
    for c in $CPUS; do jobs+=("$line|$c"); done
done <<< "$TESTS"
echo "running ${#jobs[@]} simulations, $JOBS at a time"
printf '%s\n' "${jobs[@]}" | xargs -P "$JOBS" -I{} bash -c 'run_one "$1"' _ {}

nocolor()  { sed 's/\x1b\[[0-9;]*m//g' "$1" 2>/dev/null; }
digest()   { local h; h=$(cat); if [ -z "$h" ]; then echo "-"; else echo "$h" | md5sum | cut -c1-6; fi; }
status() {                                     # pass / FAIL:<code> / TIMEOUT / ERROR
    local d=$1 e l
    e=$(cat "$d/exit" 2>/dev/null)
    if [ "$e" = 124 ] || [ "$e" = 137 ]; then echo TIMEOUT; return; fi
    l=$(grep -a 'Exiting @ tick' "$d/run.log" 2>/dev/null | tail -1)
    case "$l" in
        *"m5_exit instruction"*) echo pass ;;
        *"m5_fail instruction"*) echo "FAIL$(echo "$l" | sed -n 's/.*(code \([0-9]*\)).*/:\1/p')" ;;
        *) echo ERROR ;;
    esac
}
cmds()     { nocolor "$1/run.log" \
             | grep -a -o 'AddTransaction(PIM)\] cid: [0-9]* clk: [0-9]* | [A-Z0-9]* | addr: [0-9A-FX]*' \
             | sed -E 's/clk: [0-9]+ //' | sort | digest; }        # same commands (multiset)
launches() { grep -a -o 'PIM \(dispatch\|gemv\) .*' "$1/dbg.log" 2>/dev/null \
             | sed -E 's/ token=[0-9]+//' | digest; }             # same launches, same order
squashed() { local n; n=$(grep -a -c 'returned by squashed' "$1/dbg.log" 2>/dev/null); echo "${n:-0}"; }
fcount()   { local f=$1/dbg.log
             echo "$(grep -a -c "Enqueueing address $((0x81000140))\$" "$f" 2>/dev/null)/$(grep -a -c "Enqueueing address $((0x81000240))\$" "$f" 2>/dev/null)"; }

{
echo "validation $(date '+%F %T'), gem5: $G5 ($(date -r "$G5" '+%F %T' 2>/dev/null))"
printf "%-10s %-9s %-9s %-9s %-9s %-9s %s\n" test timing minor o3 commands launches check
all=1
while IFS= read -r line; do
    t=${line%%|*}; picked "$t" || continue
    st=(); ch=(); la=(); extra=""
    for c in $CPUS; do
        d=$OUT/${t}_$c
        s=$(status "$d"); st+=("$s"); [ "$s" = pass ] || all=0
        ch+=("$(cmds "$d")"); la+=("$(launches "$d")")
    done
    sc=same; { [ "${ch[0]}" = "${ch[1]}" ] && [ "${ch[0]}" = "${ch[2]}" ]; } || { sc=DIFF; all=0; }
    sl=same; { [ "${la[0]}" = "${la[1]}" ] && [ "${la[0]}" = "${la[2]}" ]; } || { sl=DIFF; all=0; }
    case $t in
        fence_cbo) extra="inv/flush line DRAM accesses (expect 2/3):"
                   for c in $CPUS; do
                       v=$(fcount "$OUT/${t}_$c"); extra="$extra $v"; [ "$v" = 2/3 ] || all=0
                   done ;;
        sweep)     extra="O3 launches squashed after taking a token: $(squashed "$OUT/${t}_o3")" ;;
        wrongpath) n=$(squashed "$OUT/${t}_o3")
                   extra="O3 launches squashed after taking a token: $n (need >= 64)"
                   [ "$n" -ge 64 ] || all=0 ;;
    esac
    printf "%-10s %-9s %-9s %-9s %-9s %-9s %s\n" "$t" "${st[@]}" "$sc" "$sl" "$extra"
done <<< "$TESTS"
if [ $all = 1 ]; then echo "ALL PASS"; else echo "NOT ALL PASS: logs in $OUT/<test>_<cpu>/"; fi
echo "binaries:"
md5sum $TP/pim_baremetal/pim_baremetal $TP/pim_wrongpath/pim_wrongpath $TP/pim_issue/iss_16 2>/dev/null
} | tee "$OUT/summary.txt"
