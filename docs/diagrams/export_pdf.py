#!/usr/bin/env python3
"""Export an .excalidraw file to a tight vector PDF for LaTeX (\\includegraphics).

skip -- plumbing. Step 1 draws the figure with Excalidraw 0.18.0 (the version in
the VS Code extension, loaded from esm.sh) in headless Windows Edge and takes the
SVG; step 2 prints a static page holding only that SVG. Needs internet for step 1.
Usage: python3 export_pdf.py pim_gemv_sequencer.excalidraw [out.pdf]
"""
import json, math, os, re, subprocess, sys, tempfile

EDGE = "/mnt/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"

RENDER = """<!doctype html><html><head><meta charset="utf-8">
<script type="importmap">{"imports":{
 "react":"https://esm.sh/react@18.3.1","react-dom":"https://esm.sh/react-dom@18.3.1",
 "react/jsx-runtime":"https://esm.sh/react@18.3.1/jsx-runtime",
 "react-dom/client":"https://esm.sh/react-dom@18.3.1/client"}}</script>
</head><body><script type="module">
window.EXCALIDRAW_ASSET_PATH = "https://esm.sh/@excalidraw/excalidraw@0.18.0/dist/prod/";
const scene = __SCENE__;
const X = await import("https://esm.sh/@excalidraw/excalidraw@0.18.0?external=react,react-dom");
document.body.appendChild(await X.exportToSvg({elements: scene.elements, files: scene.files || {},
  appState: {exportBackground: true, viewBackgroundColor: "#ffffff"}, exportPadding: 10}));
</script></body></html>"""

PRINT = """<!doctype html><html><head><meta charset="utf-8"><style>
@page {{ size: {w}px {h}px; margin: 0 }}
html, body {{ margin: 0; width: {w}px; height: {h}px; overflow: hidden }}
svg {{ display: block }}
* {{ -webkit-print-color-adjust: exact; print-color-adjust: exact }}
</style></head><body>{svg}</body></html>"""

def win(p):
    return subprocess.check_output(["wslpath", "-w", p], text=True).strip()

def edge(*args):
    return subprocess.run([EDGE, "--headless=new", "--disable-gpu", *args],
                          capture_output=True, text=True)

def url(p):
    return "file:///" + win(p).replace("\\", "/")

def main(src, out=None):
    out = os.path.abspath(out or os.path.splitext(src)[0] + ".pdf")
    tmp = tempfile.mkdtemp()
    page = os.path.join(tmp, "render.html")
    with open(page, "w") as f:
        f.write(RENDER.replace("__SCENE__", json.dumps(json.load(open(src)))))
    # virtual time: a big budget only waits as long as the page is still busy
    dom = edge("--virtual-time-budget=300000", "--dump-dom", url(page)).stdout
    i, j = dom.find("<svg"), dom.find("</svg>")
    if i < 0 or j < 0:
        sys.exit("no SVG: Excalidraw did not finish drawing (no internet?)")
    svg = dom[i:j + len("</svg>")]
    w, h = (math.ceil(float(v)) for v in
            re.search(r'width="([\d.]+)" height="([\d.]+)"', svg).groups())
    static = os.path.join(tmp, "print.html")
    with open(static, "w") as f:
        f.write(PRINT.format(w=w, h=h, svg=svg))
    edge("--no-pdf-header-footer", f"--print-to-pdf={win(out)}", url(static))
    print(f"wrote {out} ({w} x {h} px)")

if __name__ == "__main__":
    main(*sys.argv[1:3])
