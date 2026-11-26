"""Shared look of the thesis figures: black lines, provenance by fill, IBM Plex type.

white = standard, 45-degree hatch = PIMony, sand = this work. A figure script draws with
the primitives below and calls save(), which writes the SVG and exports PDF + PNG.
"""
import base64, math, os, re, struct, subprocess, sys, tempfile, time, urllib.request, uuid, zlib

# ---------------------------------------------------------------- style
INK, SUB, WHITE = "#141414", "#4d4d4d", "#ffffff"
BLUE = "#3b7bb8"                            # charts: slightly lighter than the original #1864ab
NEW, WASH = "#f8cba0", "#fef2e6"             # this work (muted orange): block fill, container wash
HATCH, HATCH_GAP = "#808080", 4.0            # PIMony
LIFE = "#6b6b6b"                             # lifeline
AWAKE, SLEEP, GRID = "#495057", BLUE, "#e2e2e2"  # charts: CPU awake charcoal, CPU asleep blue, grid
SW, SW_BOX, SW_SEQ, SW_WIRE = 1.2, 1.5, 2.0, 1.35
HEAD_L, HEAD_W = 8.5, 6.5                    # arrowhead length, width

out, warn = [], []

# ---------------------------------------------------------------- fonts (skip -- plumbing)
# IBM Plex (OFL) from pinned fontsource builds; cached outside the repo, embedded in the SVG
FONT_DIR = os.path.expanduser("~/.cache/thesis-fonts")
FONTS = {("sans", 400): ("ibm-plex-sans", "5.2.8", "normal"),
         ("sans", 500): ("ibm-plex-sans", "5.2.8", "normal"),
         ("sans", 600): ("ibm-plex-sans", "5.2.8", "normal"),
         ("mono", 500): ("ibm-plex-mono", "5.2.7", "normal"),
         ("ital", 400): ("ibm-plex-sans", "5.2.8", "italic"),
         ("ital", 500): ("ibm-plex-sans", "5.2.8", "italic")}
_faces, _used = {}, set()

def _woff(data):
    tabs = {}
    for i in range(struct.unpack(">H", data[12:14])[0]):
        tag, off, clen, olen, _ = struct.unpack(">4sIIII", data[44 + 20 * i:64 + 20 * i])
        raw = data[off:off + clen]
        tabs[tag.decode("latin-1")] = zlib.decompress(raw) if clen < olen else raw
    return tabs

def _cmap(t):
    subs = {}
    for i in range(struct.unpack(">H", t[2:4])[0]):
        _, _, off = struct.unpack(">HHI", t[4 + 8 * i:12 + 8 * i])
        subs[struct.unpack(">H", t[off:off + 2])[0]] = off
    m = {}
    if 12 in subs:
        off = subs[12]
        for k in range(struct.unpack(">I", t[off + 12:off + 16])[0]):
            a, b, g = struct.unpack(">III", t[off + 16 + 12 * k:off + 28 + 12 * k])
            m.update({c: g + c - a for c in range(a, b + 1)})
        return m
    off = subs[4]
    n2 = struct.unpack(">H", t[off + 6:off + 8])[0]
    n = n2 // 2
    ends = struct.unpack(f">{n}H", t[off + 14:off + 14 + n2])
    starts = struct.unpack(f">{n}H", t[off + 16 + n2:off + 16 + 2 * n2])
    deltas = struct.unpack(f">{n}h", t[off + 16 + 2 * n2:off + 16 + 3 * n2])
    ro = off + 16 + 3 * n2
    rofs = struct.unpack(f">{n}H", t[ro:ro + n2])
    for s in range(n):
        for c in range(starts[s], ends[s] + 1):
            if c == 0xFFFF:
                continue
            if rofs[s] == 0:
                m[c] = (c + deltas[s]) & 0xFFFF
            else:
                a = ro + 2 * s + rofs[s] + 2 * (c - starts[s])
                g = struct.unpack(">H", t[a:a + 2])[0]
                m[c] = (g + deltas[s]) & 0xFFFF if g else 0
    return m

class Face:
    def __init__(self, fam, wt):
        pkg, ver, style = FONTS[(fam, wt)]
        fn = os.path.join(FONT_DIR, f"{pkg}-latin-{wt}-{style}.woff")
        if not os.path.exists(fn):
            os.makedirs(FONT_DIR, exist_ok=True)
            urllib.request.urlretrieve(f"https://cdn.jsdelivr.net/npm/@fontsource/{pkg}@{ver}"
                                       f"/files/{pkg}-latin-{wt}-{style}.woff", fn)
        data = open(fn, "rb").read()
        t = _woff(data)
        self.upem = struct.unpack(">H", t["head"][18:20])[0]
        nhm = struct.unpack(">H", t["hhea"][34:36])[0]
        adv = struct.unpack(f">{2 * nhm}H", t["hmtx"][:4 * nhm])[::2]
        self.adv = {chr(c): adv[min(g, nhm - 1)] for c, g in _cmap(t["cmap"]).items()}
        self.cap = struct.unpack(">h", t["OS/2"][88:90])[0] / self.upem
        self.css = (f"@font-face{{font-family:Fig{fam.title()};font-weight:{wt};"
                    f"src:url(data:font/woff;base64,{base64.b64encode(data).decode()}) format('woff')}}")

    def width(self, s, size, ls=0):
        miss = sorted({c for c in s if c not in self.adv})
        if miss:
            warn.append(f"no glyph for {miss!r} in {s!r}")
        return sum(self.adv.get(c, self.upem) for c in s) / self.upem * size + ls * len(s)

def face(fam, wt):
    """Loaded on first use; only used faces are embedded."""
    _used.add((fam, wt))
    if (fam, wt) not in _faces:
        _faces[(fam, wt)] = Face(fam, wt)
    return _faces[(fam, wt)]

# ---------------------------------------------------------------- primitives
def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

def tw(s, size, fam="sans", wt=400, ls=0):
    return face(fam, wt).width(s, size, ls)

def text(x, y, s, size, fam="sans", wt=400, fill=INK, anchor="start", box=None, ls=0, rot=None):
    """y is the baseline; x is the start, middle or end per anchor."""
    w = tw(s, size, fam, wt, ls)
    left = {"start": x, "middle": x - w / 2, "end": x - w}[anchor]
    if box and (left < box[0] - 0.5 or left + w > box[1] + 0.5):
        warn.append(f"overflow: {s!r} [{left:.0f}..{left + w:.0f}] not in [{box[0]:.0f}..{box[1]:.0f}]")
    extra = (f' letter-spacing="{ls}"' if ls else "") + (f' transform="rotate({rot})"' if rot else "")
    out.append(f'<text x="{x:.2f}" y="{y:.2f}" font-family="Fig{fam.title()}" font-size="{size}" '
               f'font-weight="{wt}" fill="{fill}" text-anchor="{anchor}"{extra}>{esc(s)}</text>')
    return left, w

def ctext(x, cy, s, size, fam="sans", wt=400, fill=INK, anchor="middle", box=None, ls=0, rot=None):
    """Cap height centred on cy."""
    base = cy + face(fam, wt).cap * size / 2
    r = f"{rot} {x:.2f} {cy:.2f}" if rot else None
    return text(x, base, s, size, fam, wt, fill, anchor, box, ls, r)

_TOK = re.compile(r"`([^`]+)`|\*([^*]+)\*|~([^~]+)~|\^(.)|_(.)|([^`*~^_]+)")

def _segs(s, size, wt, fill):
    segs = []                                                     # text, family, weight, fill, size, dy, dx
    for mono, ital, grey, sup, sub, plain in _TOK.findall(s):
        if mono:
            segs.append((mono, "mono", 500, fill, size, 0, 0))
        elif ital:
            segs.append((ital, "ital", wt, fill, size, 0, 0))
        elif grey:
            segs.append((grey, "sans", 400, SUB, size, 0, 0))
        elif sup:                                                 # italic correction after a slanted letter
            dx = 0.1 * size if segs and segs[-1][1] == "ital" else 0
            segs.append((sup, "sans", wt, fill, size * 0.68, -0.42 * size, dx))
        elif sub:
            segs.append((sub, "sans", wt, fill, size * 0.68, 0.22 * size, 0))
        else:
            segs.append((plain, "sans", wt, fill, size, 0, 0))
    return segs

def rich_w(s, size, wt=400):
    return sum(tw(t, sz, f, w) + dx for t, f, w, _, sz, _, dx in _segs(s, size, wt, INK))

def rich(x, cy, s, size, wt=400, anchor="middle", box=None, plate=False, fill=INK):
    """Mixed run, cap centred on cy. Markup: `mono`, *italic*, ~grey~, ^X superscript, _X subscript."""
    segs = _segs(s, size, wt, fill)
    total = sum(tw(t, sz, f, w) + dx for t, f, w, _, sz, _, dx in segs)
    left = {"start": x, "middle": x - total / 2, "end": x - total}[anchor]
    if box and (left < box[0] - 0.5 or left + total > box[1] + 0.5):
        warn.append(f"overflow: {s!r} [{left:.0f}..{left + total:.0f}] not in [{box[0]:.0f}..{box[1]:.0f}]")
    cap = face("sans", wt).cap * size
    base = cy + cap / 2
    if plate:                                                     # knock a hatch out under the run
        rect(left - 5, base - cap - 4, total + 10, cap + 8, WHITE, 0, rx=1.5)
    spans, shift = [], 0
    for t, f, w, fill, sz, dy, dx in segs:
        d, shift = dy - shift, dy
        spans.append(f'<tspan font-family="Fig{f.title()}" font-weight="{w}" font-size="{sz:g}" '
                     f'fill="{fill}"' + (f' dy="{d:.2f}"' if d else "") + (f' dx="{dx:.2f}"' if dx else "")
                     + f'>{esc(t)}</tspan>')
    out.append(f'<text x="{left:.2f}" y="{base:.2f}" style="white-space:pre">{"".join(spans)}</text>')
    return left, total

def title2(x, cy, a, b, anchor="middle", sa=16, sb=14):
    """Bold name + grey qualifier on one baseline."""
    wa, wb = tw(a, sa, "sans", 600), tw(b, sb)
    left = x - (wa + wb) / 2 if anchor == "middle" else x
    base = cy + face("sans", 600).cap * sa / 2
    out.append(f'<text x="{left:.2f}" y="{base:.2f}" font-family="FigSans" style="white-space:pre">'
               f'<tspan font-size="{sa}" font-weight="600" fill="{INK}">{esc(a)}</tspan>'
               f'<tspan font-size="{sb}" font-weight="400" fill="{SUB}">{esc(b)}</tspan></text>')
    return left, wa + wb

def rect(x, y, w, h, fill=WHITE, sw=SW, dash=None, rx=0, stroke=INK):
    a = f' stroke="{stroke}" stroke-width="{sw}"' if sw else ""
    a += (f' stroke-dasharray="{dash}"' if dash else "") + (f' rx="{rx}"' if rx else "")
    out.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" fill="{fill}"{a}/>')

def line(x1, y1, x2, y2, sw=0.5, color=INK):
    out.append(f'<path d="M{x1:.2f} {y1:.2f}L{x2:.2f} {y2:.2f}" stroke="{color}" stroke-width="{sw}"/>')

def lifeline(x, y1, y2, skip=()):
    """Dashed lifeline, broken where entries sit; gaps under 8 px are not drawn."""
    d, y = [], y1
    for a, b in sorted(skip) + [(y2, y2)]:
        if a - y >= 8:
            d.append(f"M{x:.2f} {y:.2f}L{x:.2f} {a:.2f}")
        y = max(y, b)
    out.append(f'<path d="{"".join(d)}" stroke="{LIFE}" stroke-width="1" stroke-dasharray="3 3"/>')

def hatch(x, y, w, h, back=False, color=HATCH):
    """45-degree lines clipped to the box ('/', or '\\' if back); phase is global so neighbours line up."""
    step = HATCH_GAP * math.sqrt(2)
    d = []
    if back:                                                      # lines x - y = k
        k = math.ceil((x - y - h) / step) * step
        while k < x + w - y:
            lo, hi = max(x, k + y), min(x + w, k + y + h)
            if hi - lo > 0.2:
                d.append(f"M{lo:.2f} {lo - k:.2f}L{hi:.2f} {hi - k:.2f}")
            k += step
    else:                                                         # lines x + y = k
        k = math.ceil((x + y) / step) * step
        while k < x + y + w + h:
            lo, hi = max(x, k - y - h), min(x + w, k - y)
            if hi - lo > 0.2:
                d.append(f"M{lo:.2f} {k - lo:.2f}L{hi:.2f} {k - hi:.2f}")
            k += step
    out.append(f'<path d="{"".join(d)}" stroke="{color}" stroke-width="0.7" fill="none"/>')

def box(x, y, w, h, kind="std", sw=SW):
    """Provenance by fill: std = white, pim = hatched, new = sand."""
    if kind == "pim":
        rect(x, y, w, h, WHITE, 0)
        hatch(x, y, w, h)
        rect(x, y, w, h, "none", sw)
    else:
        rect(x, y, w, h, NEW if kind == "new" else WHITE, sw)

def seg(x, y, w, h, kind, sw=0):
    """Chart segment. awake = charcoal, busy = blue stripes, alone = blue stripes
    (PIM busy, no CPU), fence = orange, sleep = blue, idle = white."""
    if w <= 0 or h <= 0:
        return
    if kind in ("busy", "alone"):
        rect(x, y, w, h, WHITE, 0)
        hatch(x, y, w, h, color=BLUE)
        if sw:
            rect(x, y, w, h, "none", sw, stroke=BLUE)
    else:
        rect(x, y, w, h, {"awake": AWAKE, "fence": NEW, "sleep": SLEEP, "idle": WHITE}[kind], sw)

def key(x, y, kind, name, size=18):
    """Chart legend entry: 30 x 20 swatch at (x, y), label after it (rich markup, \\n allowed)."""
    if kind == "dash":
        out.append(f'<path d="M{x:.2f} {y + 10:.2f}H{x + 30:.2f}" stroke="{INK}" stroke-width="1.5" '
                   f'stroke-dasharray="6 4"/>')
    else:
        seg(x, y, 30, 20, kind, 0.7 if kind in ("busy", "alone", "idle") else 0)
    for i, s in enumerate(name.split("\n")):
        rich(x + 40, y + 10 + i * size * 1.25, s, size, anchor="start")
    return 40 + max(rich_w(s, size) for s in name.split("\n"))

def block(x, y, w, h, title, sub=(), kind="std", ts=16, ss=13, wt=500):
    box(x, y, w, h, kind)
    rows = [(s, ts, wt, INK) for s in title.split("\n")] + [(s, ss, 400, SUB) for s in sub]
    leads = [0] + [1.28 * r[1] for r in rows[1:]]
    cap0 = face("sans", wt).cap * ts
    base = y + (h - cap0 - sum(leads)) / 2 + cap0
    if kind == "pim":                                             # one plate knocks the hatch out
        w_ = max(tw(s, size, "sans", wt_) for s, size, wt_, _ in rows)
        rect(x + w / 2 - w_ / 2 - 5, base - cap0 - 4, w_ + 10, cap0 + sum(leads) + 8, WHITE, 0, rx=1.5)
    for (s, size, wt_, col), lead in zip(rows, leads):
        base += lead
        text(x + w / 2, base, s, size, "sans", wt_, col, "middle", box=(x + 4, x + w - 4))

def _toward(a, b, r):
    d = math.dist(a, b)
    return (a[0] + (b[0] - a[0]) * r / d, a[1] + (b[1] - a[1]) * r / d)

def _head(tip, frm):
    """Stealth arrowhead; returns where the shaft should stop (inside the head)."""
    d = math.dist(tip, frm)
    ux, uy = (tip[0] - frm[0]) / d, (tip[1] - frm[1]) / d
    bx, by = tip[0] - ux * HEAD_L, tip[1] - uy * HEAD_L
    nx, ny = tip[0] - ux * HEAD_L * 0.62, tip[1] - uy * HEAD_L * 0.62
    px, py = -uy * HEAD_W / 2, ux * HEAD_W / 2
    out.append(f'<path d="M{tip[0]:.2f} {tip[1]:.2f}L{bx + px:.2f} {by + py:.2f}L{nx:.2f} {ny:.2f}'
               f'L{bx - px:.2f} {by - py:.2f}Z" fill="{INK}"/>')
    return (tip[0] - ux * HEAD_L * 0.45, tip[1] - uy * HEAD_L * 0.45)

def wire(pts, end=True, start=False, sw=SW_WIRE, r=5):
    """Orthogonal wire with rounded bends and stealth heads."""
    pts = [tuple(p) for p in pts]
    if end:
        pts[-1] = _head(pts[-1], pts[-2])
    if start:
        pts[0] = _head(pts[0], pts[1])
    d = f"M{pts[0][0]:.2f} {pts[0][1]:.2f}"
    for a, b, c in zip(pts, pts[1:], pts[2:]):
        rr = min(r, math.dist(a, b) / 2, math.dist(b, c) / 2)
        p, q = _toward(b, a, rr), _toward(b, c, rr)
        d += f"L{p[0]:.2f} {p[1]:.2f}Q{b[0]:.2f} {b[1]:.2f} {q[0]:.2f} {q[1]:.2f}"
    d += f"L{pts[-1][0]:.2f} {pts[-1][1]:.2f}"
    out.append(f'<path d="{d}" stroke="{INK}" stroke-width="{sw}" fill="none"/>')

def badge(cx, cy, n, halo=WHITE):
    """Step marker, numbered in order."""
    out.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="10" fill="{INK}" stroke="{halo}" stroke-width="2"/>')
    ctext(cx, cy, str(n), 12.5, "sans", 600, WHITE)

def dot(x, y):
    out.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="3.2" fill="{INK}"/>')

def vdots(x, cy):
    for dy in (-3.6, 0, 3.6):
        out.append(f'<circle cx="{x:.2f}" cy="{cy + dy:.2f}" r="0.95" fill="{SUB}"/>')

def stack(x, y, w, h, n=2, d=6):
    """Shadow copies behind a box: one copy of the box per channel."""
    for i in range(n, 0, -1):
        rect(x + i * d, y - i * d, w, h, WHITE, 1.0)

def gate(cx, cy, label):
    """Condition tag: a flat hexagon."""
    hw, hh = tw(label, 13) / 2 + 10, 10
    pts = [(cx - hw, cy), (cx - hw + 6, cy - hh), (cx + hw - 6, cy - hh), (cx + hw, cy),
           (cx + hw - 6, cy + hh), (cx - hw + 6, cy + hh)]
    out.append(f'<path d="M{"L".join(f"{x:.2f} {y:.2f}" for x, y in pts)}Z" fill="{NEW}" '
               f'stroke="{INK}" stroke-width="{SW}"/>')
    ctext(cx, cy, label, 13)
    return hw

def nameplate(cx, cy, mono, sans, size=14.5):
    """Black plate: on a container's top edge, or heading a lane."""
    wa, wb = (tw(mono, size, "mono", 500) if mono else 0), tw(sans, size, "sans", 600)
    w = wa + wb + 30
    rect(cx - w / 2, cy - 12.5, w, 25, INK, 0, rx=3)
    base = cy + face("sans", 600).cap * size / 2
    m = f'<tspan font-family="FigMono" font-weight="500">{esc(mono)}</tspan>' if mono else ""
    out.append(f'<text x="{cx - w / 2 + 15:.2f}" y="{base:.2f}" font-size="{size}" fill="{WHITE}" '
               f'style="white-space:pre">{m}<tspan font-family="FigSans" font-weight="600">{esc(sans)}</tspan></text>')

def legend(cx, y, items, size=13.5):
    """Swatch + name per (kind, name), centred on cx."""
    total = sum(26 + 9 + tw(n, size) for _, n in items) + 36 * (len(items) - 1)
    lx = cx - total / 2
    for kind, name in items:
        box(lx, y, 26, 16, kind)
        ctext(lx + 35, y + 8, name, size, "sans", 400, INK, "start")
        lx += 35 + tw(name, size) + 36

# ---------------------------------------------------------------- save + export (skip -- plumbing)
# Headless Windows Edge prints the SVG to a vector PDF and screenshots a PNG. Each run gets
# a tagged profile; only processes carrying that tag are killed (Edge can hang after writing).
EDGE = "/mnt/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"
PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>__T__</title><style>
@page { size: __W__px __H__px; margin: 0 }
html, body { margin: 0; width: __W__px; height: __H__px; overflow: hidden }
svg { display: block }
* { -webkit-print-color-adjust: exact; print-color-adjust: exact }
</style></head><body>__SVG__</body></html>"""

def save(path, x0, y0, w, h, title):
    """Write path.svg; unless --svg-only, also path.pdf and a 3x path.png."""
    css = "".join(_faces[k].css for k in FONTS if k in _used)
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="{x0} {y0} {w} {h}" '
           f'text-rendering="geometricPrecision"><title>{esc(title)}</title><style>{css}</style>'
           f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" fill="{WHITE}"/>' + "".join(out) + "</svg>")
    open(path + ".svg", "w").write(svg)
    print(f"wrote {path}.svg ({w} x {h} px)")
    for w_ in warn:
        print("WARN", w_)
    out.clear(); warn.clear(); _used.clear()                      # ready for the next figure
    if "--svg-only" in sys.argv:
        return
    tmp = tempfile.mkdtemp(prefix="pimfig-")
    page = os.path.join(tmp, "page.html")
    with open(page, "w") as f:
        f.write(PAGE.replace("__T__", esc(title)).replace("__W__", str(w)).replace("__H__", str(h))
                .replace("__SVG__", svg))
    url = "file:///" + _win(page).replace("\\", "/")
    _edge(tmp, path + ".pdf", "--no-pdf-header-footer", "--virtual-time-budget=5000",
          f"--print-to-pdf={_win(path + '.pdf')}", url)
    print(f"wrote {path}.pdf")
    _edge(tmp, path + ".png", "--hide-scrollbars", "--force-device-scale-factor=3", f"--window-size={w},{h}",
          "--virtual-time-budget=5000", f"--screenshot={_win(path + '.png')}", url)
    print(f"wrote {path}.png (3x)")

def _win(p):
    return subprocess.check_output(["wslpath", "-w", p], text=True).strip()

def _ps(cmd):
    return subprocess.run(["powershell.exe", "-NoProfile", "-Command", cmd],
                          capture_output=True, text=True).stdout.strip()

def _edge(tmp, product, *args):
    tag = "pimfig-" + uuid.uuid4().hex[:10]
    prof = _ps("[IO.Path]::GetTempPath()").rstrip("\\") + "\\" + tag
    if os.path.exists(product):
        os.remove(product)
    log = open(os.path.join(tmp, tag + ".log"), "w")
    p = subprocess.Popen([EDGE, "--headless=new", "--disable-gpu", "--no-first-run",
                          f"--user-data-dir={prof}", *args],
                         stdin=subprocess.DEVNULL, stdout=log, stderr=log)
    try:
        t0, last, same = time.time(), -1, 0
        while time.time() - t0 < 120:
            size = os.path.getsize(product) if os.path.exists(product) else -1
            same = same + 1 if size > 0 and size == last else 0
            if size > 0 and (p.poll() is not None or same >= 2):
                return                                            # written and stable for 1 s
            if p.poll() is not None and time.time() - t0 > 10:
                break                                             # exited without writing
            last = size
            time.sleep(0.5)
        sys.exit(f"Edge did not write {product} (log: {log.name})")
    finally:
        _ps(f"Get-CimInstance Win32_Process -Filter \"Name='msedge.exe'\" | Where-Object "
            f"{{ $_.CommandLine -like '*{tag}*' }} | ForEach-Object {{ Stop-Process -Id $_.ProcessId "
            f"-Force -ErrorAction SilentlyContinue }}; Start-Sleep -Milliseconds 500; "
            f"Remove-Item -LiteralPath '{prof}' -Recurse -Force -ErrorAction SilentlyContinue")
        if p.poll() is None:
            p.kill()
