# Benchmark disk image — placeholder

The original SPEC CPU 2017 disk-image build resources lived here on `main`:

- `install-spec2017.sh`  — mounted the purchased SPEC ISO and ran SPEC's installer
- `post-installation.sh` — post-install setup inside the guest VM
- `runscript.sh`         — guest-side launcher invoked by gem5
- `spec-2017.json`       — Packer config that baked Ubuntu 18.04 + SPEC into a disk image

They were removed on the `riscv_cpu` branch because:
1. SPEC CPU 2017 is paid and only relevant to the x86 baseline.
2. The RISC-V port will use a free benchmark suite (likely PARSEC, GAP, or NPB).
3. Cross-compiling those for RISC-V needs a different toolchain + disk image anyway.

When we pick the new benchmark, this directory (or a renamed sibling) will hold the
equivalent build resources for it.

See `main` branch history for the original SPEC implementation.
