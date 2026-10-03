# pim_gemv test binaries — which ones match the current device

The current `pimony.cc` reads a **32 B, 32 B-aligned descriptor `{w_base, v_base, out_base}`**
at `rs1` (G6/D13). Only binaries built from `main.c` of 2026-09-17 or later pass that layout.

| binary | status |
|---|---|
| `pim_gemv_ladder` | **current** — A/B/C ladder (32x8, 128x48, 768x48), default of `fs_pim_gemv.py` |
| `gemv_32`, `gemv_64`, `gemv_128`, `gemv_192`, `pim_gemv16`, `pim_gemv_g6` | current descriptor layout (built 09-17/09-19) |
| `pim_gemv_cur`, `pim_gemv_clean`, `pim_gemv_desc`, `pim_gemv_w82` | **stale** — 16 B descriptor without `out_base` |
| `pim_gemv`, `pim_gemv_1e64/1e65`, `pim_gemv_b64/b65`, `pim_gemv_e1/e4/e8/e16/e32` | **stale** — rev-1, before the descriptor existed |

Stale binaries are kept only so older GAPS entries can be traced; do not run them on the
current build. Rebuild recipe: `reference-baremetal-testprog-build-recipe` (needs
`pim_baremetal/start.S` + `link.ld`).
