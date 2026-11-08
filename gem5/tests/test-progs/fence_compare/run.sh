#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
CC=${RISCV_CC:-riscv64-unknown-elf-gcc}
OBJDUMP=${RISCV_OBJDUMP:-riscv64-unknown-elf-objdump}
WORKDIR=tests/test-progs/fence_compare
RESULTS_DIR=${RESULTS_DIR:-$WORKDIR/results/run_$(date +%Y%m%dT%H%M%S)}
CPU_MODELS=${CPU_MODELS:-o3 minor}
LINE_COUNTS=${LINE_COUNTS:-1 16 64}
[[ ! -e "$RESULTS_DIR" ]] || { echo "Refusing to overwrite $RESULTS_DIR" >&2; exit 1; }
mkdir -p "$RESULTS_DIR/bin" "$RESULTS_DIR/source"
cp "$WORKDIR/README.md" "$WORKDIR/main.c" "$WORKDIR/run.sh" "$WORKDIR/run_matrix.py" \
   "$WORKDIR/run_config.py" "$WORKDIR/analyze.py" "$RESULTS_DIR/source/"
cp "$WORKDIR/start.S" "$WORKDIR/link.ld" "$RESULTS_DIR/source/"
"$CC" --version > "$RESULTS_DIR/compiler.txt"
"$OBJDUMP" --version > "$RESULTS_DIR/objdump.txt"
for n in $LINE_COUNTS; do
  for mode in 0 1 2; do
    binary="$RESULTS_DIR/bin/fence_cmp_m${mode}_n${n}"
    args=(-O3 -std=gnu11 -march=rv64gc_zicbom -mabi=lp64d -mcmodel=medany
      -ffreestanding -fno-builtin -fno-pie -no-pie -nostdlib -nostartfiles
      -static -Wl,--no-relax -Wl,-T,"$WORKDIR/link.ld"
      -DMODE="$mode" -DNLINES="$n"
      "$WORKDIR/start.S" "$WORKDIR/main.c" -o "$binary")
    printf '%q ' "$CC" "${args[@]}" >> "$RESULTS_DIR/compile_commands.txt"
    printf '\n' >> "$RESULTS_DIR/compile_commands.txt"
    "$CC" "${args[@]}"
    "$OBJDUMP" -D -j .text -M no-aliases "$binary" > "$binary.disasm"
  done
done
python3 "$WORKDIR/run_matrix.py" "$RESULTS_DIR" --cpus $CPU_MODELS --lines $LINE_COUNTS
python3 "$WORKDIR/analyze.py" "$RESULTS_DIR" --cpus $CPU_MODELS --lines $LINE_COUNTS
echo "Results: $RESULTS_DIR"
