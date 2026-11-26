#!/usr/bin/env python3
"""skip: plumbing. Thesis 6.2: CPU-only token sweep (Figure 6.1 and text) and W1 at 64 tokens.

GPT-3 runs: stats blocks 1-9 (the whole layer step). DRAM read = membus ReadSharedReq x 64 B.
Multiply-adds: decode 78,643,200 + 2 x 257 x 2560; prefill n x 78,643,200 + 2 x n^2 x 2560 (matmuls +
both attention products). BERT-sized runs: block 1, 7,077,888 x n + 2 x n^2 x 768.
Usage: python3 experiments/6.2_seqlen/parse.py [OUT]   OUT = run dir relative to gem5/, default m5out
Prints the numbers and writes sweep.csv, fig6_1.csv and w1_compare.csv next to this script.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from common import PERIOD, blocks, write_csv  # noqa: E402

READS, WB = "system.membus.transDist::ReadSharedReq", "system.membus.transDist::WritebackDirty"
STATS = ("simTicks", "system.cpu.numCycles", "simInsts", READS, WB)
GPT3 = [("decode", 1, "gpt3_blas_c_o3_oct")] + \
       [("prefill", n, f"gpt3_prefill_s{n}_o3_oct") for n in (4, 16, 64, 128, 256, 512)]


def gpt3_macs(kind, n):
    return 78643200 + 2 * 257 * 2560 if kind == "decode" else n * 78643200 + 2 * n * n * 2560


def sweep_row(run, n, macs, use_blocks):
    v = blocks(run, *STATS)
    s = {k: sum(v[k].get(b, 0) for b in use_blocks) for k in STATS}  # a stat gem5 did not print counts as 0
    read = s[READS] * 64
    return {"run": run, "tokens": n, "cycles_per_token": round(s["system.cpu.numCycles"] / n),
            "dram_read_mb": round(read / 1e6, 1), "gb_per_s": round(read / (s["simTicks"] * 1e-3), 2),
            "macs_per_byte": round(macs / read, 2), "ipc": round(s["simInsts"] / s["system.cpu.numCycles"], 3)}


# GPT-3 sweep, 2 MiB L2 (Figure 6.1 and text)
sweep = [dict(kind=k, **sweep_row(r, n, gpt3_macs(k, n), range(1, 10))) for k, n, r in GPT3]
write_csv(HERE / "sweep.csv", sweep)
print("GPT-3 layer, CPU only, O3, 2 MiB L2")
print(f"{'':8s} {'tokens':>6s} {'cycles/token':>13s} {'DRAM read':>10s} {'GB/s':>6s} {'MACs/byte':>9s} {'IPC':>6s}")
for r in sweep:
    print(f"{r['kind']:8s} {r['tokens']:6d} {r['cycles_per_token']:13,d} {r['dram_read_mb']:7.1f} MB"
          f" {r['gb_per_s']:6.2f} {r['macs_per_byte']:9.2f} {r['ipc']:6.3f}")
decode = sweep[0]
s256 = next(r for r in sweep if r["tokens"] == 256 and r["kind"] == "prefill")
print(f"256 tokens: {s256['macs_per_byte'] / decode['macs_per_byte']:.0f}x the decode MACs/byte")
write_csv(HERE / "fig6_1.csv", [{"kind": r["kind"], "tokens": r["tokens"], "cycles_per_token": r["cycles_per_token"],
                                 "macs_per_byte": r["macs_per_byte"]} for r in sweep])

# 32 MiB L2: the drop past 128 tokens disappears; W1's DRAM reads (stats block 5) at 256 tokens
print("\nSame prefill runs with a 32 MiB L2 (L2_SIZE=32MiB)")
for n in (128, 256, 512):
    r = sweep_row(f"gpt3_prefill_s{n}_l2_32m", n, gpt3_macs("prefill", n), range(1, 10))
    print(f"prefill {n:4d}  MACs/byte {r['macs_per_byte']:6.2f}  (2 MiB L2: "
          f"{next(x['macs_per_byte'] for x in sweep if x['tokens'] == n and x['kind'] == 'prefill'):.2f})")
w1 = {name: blocks(run, READS)[READS][5] * 64 / 1e6
      for name, run in (("2 MiB", "gpt3_prefill_s256_o3_oct"), ("32 MiB", "gpt3_prefill_s256_l2_32m"))}
print(f"W1 at 256 tokens reads {w1['2 MiB']:.1f} MB with the 2 MiB L2, {w1['32 MiB']:.1f} MB with 32 MiB;"
      f" W1's FP16 weights {2560 * 10240 * 2 / 1e6:.1f} MB + FP32 output {256 * 10240 * 4 / 1e6:.1f} MB")

# BERT-sized layer, FP32 weights, 128-512 tokens
print("\nBERT-sized layer, CPU only, O3, FP32 weights")
for run, n in (("o3-s128-l2big", 128), ("l2big-s256_oct", 256), ("l2big-s384_oct", 384), ("l2big-s512_oct", 512)):
    r = sweep_row(run, n, 7077888 * n + 2 * n * n * 768, (1,))
    print(f"{n:4d} tokens  MACs/byte {r['macs_per_byte']:6.2f}")

# W1 at 64 tokens: OpenBLAS GEMM on 64 vectors vs pim.gemv per vector (3 jobs each)
w1c = []
for backend, run in (("cpu", "w1_compare_cpu_64_o3"), ("pim", "w1_compare_pim_64_o3")):
    cycles = blocks(run, "simTicks")["simTicks"][1] // PERIOD
    w1c.append({"backend": backend, "run": run, "cycles_64_vectors": cycles, "cycles_per_vector": round(cycles / 64)})
write_csv(HERE / "w1_compare.csv", w1c)
cpu, pim = w1c[0]["cycles_64_vectors"], w1c[1]["cycles_64_vectors"]
print(f"\nW1, 64 vectors: CPU {cpu / 64 / 1e6:.3f} M cycles per vector, PIM {pim / 64 / 1e6:.3f} M: PIM {cpu / pim:.2f}x faster")
