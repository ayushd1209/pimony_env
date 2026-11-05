#!/bin/bash
# skip: plumbing. Rebuild the binaries the validation suite needs that changed:
# the address-space test (now checks pim.wait's result), the new wrong-path test,
# and iss_16 (stale since the CSR move: it still wrote 0x801 and hangs on the new gem5).
# Recipe of the other bare-metal tests: pim_baremetal/start.S + link.ld, -O2 rv64gc medany.
# Run on the host (the container has no RISC-V toolchain):
#   bash gem5/tests/test-progs/build_validation.sh
set -eu
cd "$(dirname "$0")"
CC=${CC:-riscv64-unknown-elf-gcc}
OBJDUMP=${OBJDUMP:-riscv64-unknown-elf-objdump}
F="-O2 -march=rv64gc -mabi=lp64d -mcmodel=medany -nostdlib -nostartfiles \
   -T pim_baremetal/link.ld pim_baremetal/start.S"
$CC $F -o pim_baremetal/pim_baremetal pim_baremetal/main.c -lgcc
$CC $F -o pim_wrongpath/pim_wrongpath pim_wrongpath/main.c -lgcc
$CC $F -o pim_issue/iss_16 pim_issue/main.c -lgcc
for b in pim_baremetal/pim_baremetal pim_wrongpath/pim_wrongpath pim_issue/iss_16; do
    printf "%-30s csrw 0x5c0: %s   csrw 0x801: %s\n" "$b" \
        "$($OBJDUMP -d "$b" | grep -c 'csrw\s*0x5c0' || true)" \
        "$($OBJDUMP -d "$b" | grep -c 'csrw\s*0x801' || true)"
done
