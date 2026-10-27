#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

compiler=${W1_CPU_CC:-/opt/riscv/bin/riscv64-unknown-linux-gnu-gcc}
blas=${W1_BLAS_ROOT:-/opt/openblas-zvl256b}
flags=(-O3 -march=rv64gcv_zfh -mabi=lp64d -mcmodel=medany -static
       -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math
       -I"$blas/include")
suffix=
if [[ ${W1_FAST_INIT:-0} == 1 ]]; then flags+=(-DW1_FAST_INIT); suffix=_fast; fi

for vectors in 1 64; do
    "$compiler" "${flags[@]}" -DW1_VECTORS="$vectors" -DW1_CHECK \
        -o "w1_cpu_${vectors}${suffix}" cpu.c "$blas/lib/libopenblas.a" -lm
done
"$compiler" "${flags[@]}" -DW1_INPUTS=32 -DW1_OUTPUTS=128 -DW1_VECTORS=4 -DW1_CHECK \
    -o "w1_cpu_check${suffix}" cpu.c "$blas/lib/libopenblas.a" -lm

printf 'Built CPU benchmarks and the small reference-check case.\n'
