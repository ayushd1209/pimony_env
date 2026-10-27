#!/usr/bin/env bash
# Run inside gem5-container. Override only SHGEMV's reduction instruction.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

compiler=${W1_CPU_CC:-/opt/riscv/bin/riscv64-unknown-linux-gnu-gcc}
objcopy=${W1_CPU_OBJCOPY:-${compiler%gcc}objcopy}
blas=${W1_BLAS_ROOT:-/opt/openblas-zvl256b}
source_root=${W1_OPENBLAS_SOURCE:-/opt/OpenBLAS}
mode=${1:-probe}
case "$mode" in probe|all) ;; *) echo 'Usage: bash build_ordered_cpu.sh [probe|all]' >&2; exit 2;; esac
original="$source_root/kernel/riscv64/sbgemv_t_vector.c"
ordered=diagnostics/shgemv_t_ordered.c
object=diagnostics/shgemv_t_ordered.o

# Both intrinsic-API spellings name the same reduction; preserve all other code.
sed 's/vfredusum_vs_f32m8_f32m1/vfredosum_vs_f32m8_f32m1/g' "$original" > "$ordered"
kernel_flags=(-O2 -march=rv64imafdcv_zvfh_zfh_zvl256b -mabi=lp64d
              -I"$source_root" -DHFLOAT16 -DCNAME=shgemv_t -UCOMPLEX -UDOUBLE)
"$compiler" "${kernel_flags[@]}" -c "$original" -o diagnostics/shgemv_t_original_o2.o
"$compiler" "${kernel_flags[@]}" -c "$ordered" -o "$object"
"$objcopy" -O binary --only-section=.text diagnostics/shgemv_t_original_o2.o diagnostics/shgemv_t_original_o2.text
"$objcopy" -O binary --only-section=.text "$source_root/kernel/shgemv_t.o" diagnostics/shgemv_t_installed.text
"$objcopy" -O binary --only-section=.text "$object" diagnostics/shgemv_t_ordered.text
python3 - <<'PY'
from pathlib import Path
original = Path('diagnostics/shgemv_t_original_o2.text').read_bytes()
installed = Path('diagnostics/shgemv_t_installed.text').read_bytes()
ordered = Path('diagnostics/shgemv_t_ordered.text').read_bytes()
assert original == installed, 'Rebuilt original kernel differs from installed instructions'
assert len(original) == len(ordered), 'Ordered kernel instruction layout changed'
changed = [i for i, (a, b) in enumerate(zip(original, ordered)) if a != b]
assert len(changed) == 4, changed
for i in changed:
    a = int.from_bytes(original[i - 3:i + 1], 'little')
    b = int.from_bytes(ordered[i - 3:i + 1], 'little')
    assert a ^ b == 0x08000000 and a & 0xfc00707f == 0x04001057, (i, hex(a), hex(b))
print('Verified: exactly four vfredusum instructions become vfredosum; all other instruction bytes match.')
PY

flags=(-O3 -march=rv64gcv_zfh -mabi=lp64d -mcmodel=medany -static
       -fno-math-errno -fassociative-math -fno-signed-zeros -fno-trapping-math
       -I"$blas/include" -DW1_FAST_INIT -DW1_CHECK)
"$compiler" "${flags[@]}" -DW1_INPUTS=2560 -DW1_OUTPUTS=128 -DW1_VECTORS=1 \
    -o w1_cpu_gemv_probe_ordered_fast cpu.c "$object" "$blas/lib/libopenblas.a" -lm
if [[ "$mode" == all ]]; then
for vectors in 1 64; do
    "$compiler" "${flags[@]}" -DW1_VECTORS="$vectors" \
        -o "w1_cpu_${vectors}_ordered_fast" cpu.c "$object" "$blas/lib/libopenblas.a" -lm
done
fi
sha256sum "$original" "$ordered" "$object" w1_cpu_gemv_probe_ordered_fast \
    > diagnostics/ordered_build.sha256
if [[ "$mode" == all ]]; then
    sha256sum w1_cpu_1_ordered_fast w1_cpu_64_ordered_fast >> diagnostics/ordered_build.sha256
fi
cat diagnostics/ordered_build.sha256
