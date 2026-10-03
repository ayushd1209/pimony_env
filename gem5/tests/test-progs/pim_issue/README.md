# pim_issue test binaries — which ones to run

| binary | status |
|---|---|
| `iss_16` | **current** — 16 dispatches over 16 different engines, none revisited; default of `fs_pim_issue.py` |
| `iss_32`, `iss_64`, `iss_128`, `iss_192` | current source, 32–192 dispatches over 32 engines (an engine is reused only every 32 dispatches) |
| `pim_issue16` | intermediate build (16 engines) — superseded by `iss_16` |
| `pim_issue` | **stale** — August build: 16 dispatches cycled over 4 engines |

Why `pim_issue` is stale: it revisits an engine every 4 dispatches. A DRAM refresh can queue
two MACs for one engine; PIMony treats a MAC to a busy engine as a resume and drops one
completion, so `pim.wait` never returns. Seen on Minor 2026-10-01 (16 MACs, 15 MACINTRs);
same failure as G9's 32-per-wave schedule. Not a CPU bug — the host has no per-engine busy signal.
