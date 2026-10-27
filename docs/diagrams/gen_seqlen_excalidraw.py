#!/usr/bin/env python3
"""Generate editable Excalidraw and vector previews of the CPU-only token sweep."""

import argparse
import itertools
import json
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib._afm import AFM
from matplotlib.patches import Ellipse, Polygon, Rectangle


OUT = Path(__file__).resolve().parent
PARSER = argparse.ArgumentParser(description=__doc__)
PARSER.add_argument("--performance", action="store_true",
                    help="Pair cycles per token with reuse, with decode as a fixed reference")
ARGS = PARSER.parse_args()
INK, BLUE, AXIS, GRID = "#343a40", "#1864ab", "#868e96", "#e9ecef"
TOKENS = [4, 16, 64, 128, 256, 512]
REUSE = [1.57, 5.95, 17.09, 24.14, 19.06, 18.61]
BANDWIDTH = [3.35, 2.93, 2.35, 2.07, 2.74, 2.94]
CYCLES_PER_TOKEN = [44959560, 13563564, 5913266, 4754784, 4605617, 4462854]
LEFT, RIGHT, HEIGHT = 125, 965, 220
IDS, ELEMENTS = itertools.count(1), []
with (Path(matplotlib.get_data_path()) / "fonts/pdfcorefonts/Helvetica.afm").open("rb") as font_file:
    FONT_METRICS = AFM(font_file)


def element(kind, x, y, width, height, **extra):
    n = next(IDS)
    item = {
        "id": f"seqlen-{n}", "type": kind, "x": x, "y": y,
        "width": width, "height": height, "angle": 0,
        "strokeColor": INK, "backgroundColor": "transparent",
        "fillStyle": "solid", "strokeWidth": 1, "strokeStyle": "solid",
        "roughness": 0, "opacity": 100, "groupIds": [], "frameId": None,
        "roundness": None, "seed": n * 7919, "version": 1,
        "versionNonce": n * 104729, "isDeleted": False, "boundElements": [],
        "updated": 1, "link": None, "locked": False,
    }
    item.update(extra)
    ELEMENTS.append(item)
    return item


def line(points, color=INK, width=1, dashed=False):
    x0, y0 = points[0]
    return element(
        "line", x0, y0,
        max(x for x, _ in points) - min(x for x, _ in points),
        max(y for _, y in points) - min(y for _, y in points),
        strokeColor=color, strokeWidth=width,
        strokeStyle="dashed" if dashed else "solid",
        points=[[x - x0, y - y0] for x, y in points],
        lastCommittedPoint=None, startBinding=None, endBinding=None,
        startArrowhead=None, endArrowhead=None,
    )


def text(x, y, value, size=18, color=INK, align="left"):
    # Helvetica widths, from the standard PDF core-font metrics.
    widths = [sum(FONT_METRICS.get_width_from_char_name("multiply") if c == "×"
                  else FONT_METRICS.get_width_char(ord(c)) for c in row)
              for row in value.split("\n")]
    width = max(widths) * size / 1000
    height = len(widths) * size * 1.25
    x -= {"left": 0, "center": width / 2, "right": width}[align]
    return element(
        "text", x, y, width, height, strokeColor=color, fontSize=size,
        fontFamily=2, text=value, originalText=value, textAlign=align,
        verticalAlign="top", containerId=None, lineHeight=1.25,
        baseline=round(size * 0.9), autoResize=True,
    )


def marker(x, y, color, diamond=False):
    element("diamond" if diamond else "ellipse", x - 5, y - 5, 10, 10,
            strokeColor=color, backgroundColor=color, strokeWidth=1)


def x_of(tokens):
    return LEFT + (math.log2(tokens) + 0.4) / 9.8 * (RIGHT - LEFT)


def y_of(value, baseline, maximum):
    return baseline - value / maximum * HEIGHT


def panel(baseline, maximum, heading, decode, values, labels):
    top = baseline - HEIGHT
    text(LEFT, top - 40, heading, size=20)
    element("rectangle", LEFT, top, x_of(2) - LEFT, HEIGHT,
            strokeColor="transparent", backgroundColor="#f5f6f7", strokeWidth=0)
    for value in range(0, int(maximum) + 1, 5):
        y = y_of(value, baseline, maximum)
        if value:
            line([(LEFT, y), (RIGHT, y)], GRID, width=0.8)
        text(LEFT - 12, y - 11, str(value), align="right")
    line([(x_of(2), top), (x_of(2), baseline)], "#adb5bd", dashed=True)
    line([(LEFT, top), (LEFT, baseline), (RIGHT, baseline)], AXIS)
    for token in [1, *TOKENS]:
        x = x_of(token)
        line([(x, baseline), (x, baseline + 5)], AXIS)

    marker(x_of(1), y_of(decode, baseline, maximum), INK, diamond=True)
    text(x_of(1), y_of(decode, baseline, maximum) - 30,
         "0.49" if decode < 1 else "17.9", color=INK, align="center")
    points = [(x_of(token), y_of(value, baseline, maximum))
              for token, value in zip(TOKENS, values)]
    line(points, BLUE, width=2.3)
    for (x, y), label in zip(points, labels):
        marker(x, y, BLUE)
        text(x, y - 30, label, color=BLUE, align="center")


def prefill_x(tokens):
    return LEFT + (math.log2(tokens) - 1.7) / 7.6 * (RIGHT - LEFT)


def performance_panel(baseline, maximum, step, heading, decode, values, labels):
    top = baseline - HEIGHT
    text(LEFT, top - 40, heading, size=20)
    for value in range(0, int(maximum) + 1, step):
        y = y_of(value, baseline, maximum)
        if value:
            line([(LEFT, y), (RIGHT, y)], GRID, width=0.8)
        text(LEFT - 12, y - 11, str(value), align="right")
    line([(LEFT, top), (LEFT, baseline), (RIGHT, baseline)], AXIS)
    reference_y = y_of(decode, baseline, maximum)
    line([(LEFT, reference_y), (RIGHT, reference_y)], INK, width=1.3, dashed=True)
    reference_label = "27.4" if decode > 1 else "0.49"
    text(RIGHT - 4, reference_y - 30, f"Decode reference: {reference_label}",
         color=INK, align="right", size=17)
    points = [(prefill_x(token), y_of(value, baseline, maximum))
              for token, value in zip(TOKENS, values)]
    line(points, BLUE, width=2.3)
    for (x, y), label in zip(points, labels):
        line([(x, baseline), (x, baseline + 5)], AXIS)
        marker(x, y, BLUE)
        text(x, y - 30, label, color=BLUE, align="center")


if ARGS.performance:
    line([(230, 26), (260, 26)], BLUE, width=2.3)
    marker(245, 26, BLUE)
    text(275, 14, "Prefill measurements")
    line([(570, 26), (600, 26)], INK, width=1.3, dashed=True)
    text(615, 14, "Fixed decode reference")
    costs = [value / 1e6 for value in CYCLES_PER_TOKEN]
    performance_panel(340, 50, 10, "Cycles per processed token (millions)",
                      27401792 / 1e6, costs, [f"{value:.1f}" for value in costs])
    performance_panel(665, 28, 5, "Multiply-adds per byte read from DRAM", 0.49, REUSE,
                      ["1.6", "6.0", "17.1", "24.1", "19.1", "18.6"])
    line([(prefill_x(256), y_of(19.06, 665, 28) + 9), (prefill_x(256), 560)], BLUE)
    text(775, 573, "256 tokens:\n39× decode", color=BLUE)
    for token in TOKENS:
        text(prefill_x(token), 679, str(token), align="center")
    text((LEFT + RIGHT) / 2, 731, "Prefill prompt length (tokens; log scale, base 2)",
         align="center")
    SCENE_STEM = PREVIEW_STEM = "seqlen_performance"
else:
    marker(247, 26, INK, diamond=True)
    text(263, 14, "Decode (256 cached tokens)")
    line([(596, 26), (626, 26)], BLUE, width=2.3)
    marker(611, 26, BLUE)
    text(638, 14, "Prefill (empty KV cache)")
    panel(340, 28, "Multiply-adds per byte read from DRAM", 0.49, REUSE,
          ["1.6", "6.0", "17.1", "24.1", "19.1", "18.6"])
    line([(x_of(256), y_of(19.06, 340, 28) + 9), (x_of(256), 246)], BLUE)
    text(790, 258, "256 tokens:\n39× decode", color=BLUE)
    panel(665, 21, "DRAM read bandwidth (GB/s)", 17.90, BANDWIDTH,
          [f"{value:.2f}" for value in BANDWIDTH])
    for token in [1, *TOKENS]:
        text(x_of(token), 679, str(token), align="center")
    text(x_of(1), 705, "decode", size=16, color="#495057", align="center")
    text((LEFT + RIGHT) / 2, 746,
         "Tokens processed per layer invocation (log scale, base 2)", align="center")
    SCENE_STEM, PREVIEW_STEM = "seqlen", "seqlen_excalidraw"

scene = {
    "type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
    "elements": ELEMENTS,
    "appState": {"gridSize": None, "viewBackgroundColor": "#ffffff"}, "files": {},
}
destination = OUT / f"{SCENE_STEM}.excalidraw"
destination.write_text(json.dumps(scene, indent=1) + "\n")
print(f"{destination}: {len(ELEMENTS)} editable elements")

# Render the same straight-line geometry for a portable report preview.
plt.rcParams.update({"font.family": "DejaVu Sans", "pdf.fonttype": 42, "svg.fonttype": "none"})
fig = plt.figure(figsize=(10, 7.9), dpi=150)
ax = fig.add_axes([0, 0, 1, 1], xlim=(0, 1000), ylim=(790, 0))
ax.set_axis_off()
for item in ELEMENTS:
    kind, x, y = item["type"], item["x"], item["y"]
    width, height = item["width"], item["height"]
    stroke, fill = item["strokeColor"], item["backgroundColor"]
    stroke = "none" if stroke == "transparent" else stroke
    fill = "none" if fill == "transparent" else fill
    lw = item["strokeWidth"] * 0.72
    if kind == "line":
        points = [(x + dx, y + dy) for dx, dy in item["points"]]
        ax.plot([p[0] for p in points], [p[1] for p in points], color=stroke,
                lw=lw, linestyle=(0, (4, 4)) if item["strokeStyle"] == "dashed" else "-",
                solid_capstyle="round")
    elif kind == "text":
        align = item["textAlign"]
        tx = x + {"left": 0, "center": width / 2, "right": width}[align]
        ax.text(tx, y + 1, item["text"], fontsize=item["fontSize"] * 0.72,
                color=stroke, ha=align, va="top", linespacing=1.25)
    elif kind == "rectangle":
        ax.add_patch(Rectangle((x, y), width, height, facecolor=fill, edgecolor=stroke, lw=lw))
    elif kind == "ellipse":
        ax.add_patch(Ellipse((x + width / 2, y + height / 2), width, height,
                             facecolor=fill, edgecolor=stroke, lw=lw))
    elif kind == "diamond":
        vertices = [(x + width / 2, y), (x + width, y + height / 2),
                    (x + width / 2, y + height), (x, y + height / 2)]
        ax.add_patch(Polygon(vertices, facecolor=fill, edgecolor=stroke, lw=lw))

for extension in ("pdf", "svg", "png"):
    destination = OUT / f"{PREVIEW_STEM}.{extension}"
    fig.savefig(destination, dpi=180, facecolor="white", bbox_inches="tight", pad_inches=0.03)
    if extension == "svg":
        destination.write_text("\n".join(row.rstrip() for row in destination.read_text().splitlines()) + "\n")
    print(destination)
plt.close(fig)
