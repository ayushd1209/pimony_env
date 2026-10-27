#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

compiler=${W1_PIM_CC:-riscv64-unknown-elf-gcc}
flags=(-O3 -march=rv64gcv_zfh -mabi=lp64d -mcmodel=medany -static
       -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math
       -ffunction-sections -fdata-sections -Wl,--gc-sections
       -DFREESTANDING -DNO_IO -DBAREMETAL -DPIM_LAYOUT -DPIM_GEMV
       -nostdlib -nostartfiles -T ../gpt3_bm.ld)
suffix=
if [[ ${W1_FAST_INIT:-0} == 1 ]]; then flags+=(-DW1_FAST_INIT); suffix=_fast; fi

for vectors in 1 64; do
    "$compiler" "${flags[@]}" -DW1_VECTORS="$vectors" -o "w1_pim_${vectors}${suffix}" pim.c -lgcc
done
"$compiler" "${flags[@]}" -DW1_INPUTS=32 -DW1_OUTPUTS=128 -DW1_VECTORS=4 \
    -o "w1_pim_check${suffix}" pim.c -lgcc

printf 'Built PIM benchmarks and the small completion-check case.\n'
