# Validated artifacts for the guarded comparison

These compact files accompany the final 18 performance measurements. Raw
simulation logs and earlier investigations remain in the ignored local
`../results/` and `../e2e/results/` directories; none was removed or overwritten.
The presentation and exact table are in [../README.md](../README.md).

- `measurements.tsv`: byte-for-byte copy of the final run's 18 rows, with ROI,
  instruction, write/response, guard and validation fields. All rows passed
  this performance benchmark's checks; they contain no GEMV numerical claim.
- `provenance.json`: unchanged final-run provenance, including simulator commit,
  clean source status, gem5/PIMony/ELF/config hashes and Docker image ID.
- `build_provenance.json`: original isolated build record, dependency revisions,
  build command and gem5 build information.
- `compiler.txt`, `objdump.txt`: exact tool versions used for the measured ELFs.
- `reproducibility.json`: original record of one exact reproduction per case;
  referenced directories remain under `../results/`.
- `fs_bert.py`: unchanged reference snapshot of the measured configuration.
  The harness executes this file at its original path in the pinned isolated
  checkout, never the working repository's potentially edited configuration.
- `o3_config.ini`, `minor_config.ini`: complete generated machine snapshots,
  copied from the one-line custom cases. Validation verified a common machine
  fingerprint across all modes/sizes within each CPU model. Generated workload
  and output paths identify the original runs and are not relocation settings.
- `pimony_mem.json`, `gpt3-2.7B_single_layer.json`,
  `LPDDR5X_12Gb_x16_8533_pimony.ini`: memory/model/timing parameter snapshots.
  The model filename is a configuration dependency; this benchmark runs no GPT-3 workload.
- `source_provenance.json`: measured and integrated source hashes and the small
  harness packaging changes. Benchmark instructions and startup/linker bytes
  are unchanged. The original source snapshots remain in the raw run.
- `SHA256SUMS`: integrity hashes for this compact directory and benchmark sources.
  From the parent benchmark directory, verify with `sha256sum -c validated/SHA256SUMS`.

Original final run: `../results/run_20261108_guarded_final/`.
Collection date: 8 November 2026. All cycle counts include the common warmed
release-AMO completion guard. No new simulations ran during integration.
