# skip: plumbing. Sourced by the experiments' run.sh. Defines run(); OUT = output dir relative to gem5/.
# run NAME CONFIG CPU BINARY [ARGS...]  starts gem5 in gem5-container in the background, stdout filtered.
# KEEP='<regex>' run ...  also keeps those lines in run.log. DRY=1 prints instead of starting.
# A run folder that already exists is skipped, never overwritten.
GEM5=$(cd "$(dirname "${BASH_SOURCE[0]}")/../gem5" && pwd)
OUT=${1:-m5out}
FILTER='workload|layer|out\[0|checksum|PIM State|Exiting|panic|fatal|egmentation'

run() {
  local d=$OUT/$1 cfg=$2 f=$FILTER cmd; shift 2
  if [ -e "$GEM5/$d" ]; then echo "skip $d (exists)"; return; fi
  [ -n "${KEEP:-}" ] && f="$KEEP|$f"
  cmd="mkdir -p $d && build/RISCV/gem5.opt -d $d configs/pimony/$cfg $* 2>&1 | grep -a -E '$f' > $d/run.log"
  if [ "${DRY:-0}" = 1 ]; then echo "$cmd"; return; fi
  docker exec -d -w /home/pimony/gem5 gem5-container sh -c "$cmd"
  echo "started $d"
}

[ "${DRY:-0}" = 1 ] || docker start gem5-container >/dev/null
