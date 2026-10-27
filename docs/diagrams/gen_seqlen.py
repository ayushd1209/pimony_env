#!/usr/bin/env python3
"""Plot the CPU-only GPT-3 token sweep supplied for the evaluation chapter."""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FixedLocator, MultipleLocator


OUT = Path(__file__).resolve().parent
TOKENS = [4, 16, 64, 128, 256, 512]
MACS_PER_BYTE = [1.57, 5.95, 17.09, 24.14, 19.06, 18.61]
READ_GBS = [3.35, 2.93, 2.35, 2.07, 2.74, 2.94]
DECODE = {"macs_per_byte": 0.49, "read_gbs": 17.90}
BLUE, INK, GRID = "#2e64a3", "#41464b", "#e5e7eb"

plt.rcParams.update({
    "font.family": "STIXGeneral",
    "font.size": 10,
    "axes.labelsize": 11,
    "axes.edgecolor": "#666666",
    "axes.linewidth": 0.65,
    "xtick.labelsize": 10,
    "ytick.labelsize": 10,
    "pdf.fonttype": 42,
    "svg.fonttype": "none",
    "savefig.facecolor": "white",
})

fig, axes = plt.subplots(2, 1, figsize=(6.8, 4.55), sharex=True)
fig.subplots_adjust(left=0.15, right=0.97, bottom=0.16, top=0.89, hspace=0.23)

for ax in axes:
    ax.set_xscale("log", base=2)
    ax.set_xlim(0.78, 680)
    ax.set_axisbelow(True)
    ax.grid(axis="y", color=GRID, linewidth=0.65)
    ax.axvspan(0.78, 2, color="#f4f4f4", zorder=0)
    ax.axvline(2, color="#b0b0b0", linestyle=(0, (3, 3)), linewidth=0.7)
    ax.spines[["top", "right"]].set_visible(False)
    ax.tick_params(axis="both", length=3, width=0.65)
    ax.minorticks_off()

top, bottom = axes
decode_handle, = top.plot(1, DECODE["macs_per_byte"], marker="D", color=INK,
                          markersize=5, linestyle="none", zorder=3)
prefill_handle, = top.plot(TOKENS, MACS_PER_BYTE, "o-", color=BLUE,
                           markersize=4.5, linewidth=1.5, zorder=3)
top.set_ylim(0, 28)
top.yaxis.set_major_locator(MultipleLocator(5))
top.set_ylabel("Multiply-adds per\nbyte read from DRAM")
top.annotate("0.49", (1, 0.49), xytext=(0, 9), textcoords="offset points",
             ha="center", color=INK)
for tokens, value in [(4, 1.57), (16, 5.95), (64, 17.09), (128, 24.14), (512, 18.61)]:
    top.annotate(f"{value:.1f}", (tokens, value), xytext=(0, 8),
                 textcoords="offset points", ha="center", color=BLUE)
top.annotate("256 tokens: 19.1\n39× decode", xy=(256, 19.06), xytext=(150, 7.5),
             color=BLUE, ha="left", va="center", linespacing=1.25,
             arrowprops={"arrowstyle": "-", "color": BLUE, "lw": 0.8})

bottom.plot(1, DECODE["read_gbs"], marker="D", color=INK,
            markersize=5, linestyle="none", zorder=3)
bottom.plot(TOKENS, READ_GBS, "o-", color=BLUE, markersize=4.5,
            linewidth=1.5, zorder=3)
bottom.set_ylim(0, 21)
bottom.yaxis.set_major_locator(MultipleLocator(5))
bottom.set_ylabel("DRAM read bandwidth\n(GB/s)")
bottom.annotate("17.9", (1, 17.90), xytext=(0, 9), textcoords="offset points",
                ha="center", color=INK)
for tokens, value in zip(TOKENS, READ_GBS):
    bottom.annotate(f"{value:.2f}", (tokens, value), xytext=(0, 8),
                    textcoords="offset points", ha="center", color=BLUE, fontsize=9)

bottom.xaxis.set_major_locator(FixedLocator([1, *TOKENS]))
bottom.set_xticklabels(["1\n(decode)", *map(str, TOKENS)])
bottom.set_xlabel(r"Tokens processed per layer invocation ($\log_2$ scale)", labelpad=8)
fig.legend([decode_handle, prefill_handle],
           ["Decode (256 cached tokens)", "Prefill (empty KV cache)"],
           loc="upper center", bbox_to_anchor=(0.54, 0.99), ncol=2,
           frameon=False, handlelength=1.7, columnspacing=2.0, fontsize=10)

for extension in ("pdf", "svg", "png"):
    destination = OUT / f"seqlen.{extension}"
    fig.savefig(destination, dpi=200, bbox_inches="tight", pad_inches=0.06)
    if extension == "svg":
        destination.write_text("\n".join(row.rstrip() for row in destination.read_text().splitlines()) + "\n")
    print(destination)
plt.close(fig)
