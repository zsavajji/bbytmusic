#!/usr/bin/env python3
"""Draws the app's icons into app/assets/images with no library but zlib: action icons, white on
transparent, 81 x 81 px (9 du on a Q10); and the artwork placeholder, a grey note on #262626 as
the stock player shows for a track without a cover. Run again after changing a shape.

    python3 scripts/gen-icons.py            # writes the icons
    python3 scripts/gen-icons.py sheet.png  # also one sheet of them all on grey, to look at

A shape is a function (x, y) -> bool over the unit square, y down.
"""
import math, os, struct, sys, zlib

SS = 4   # samples per pixel along each axis


# ---- shapes -------------------------------------------------------------------------------
def rect(x0, y0, x1, y1):
    return lambda x, y: x0 <= x <= x1 and y0 <= y <= y1


def circle(cx, cy, r):
    return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def ring(cx, cy, r, w):
    return lambda x, y: (r - w / 2) ** 2 <= (x - cx) ** 2 + (y - cy) ** 2 <= (r + w / 2) ** 2


def ellipse(cx, cy, rx, ry):
    return lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1


def seg(x0, y0, x1, y1, w):   # a line with round ends
    dx, dy = x1 - x0, y1 - y0
    n = dx * dx + dy * dy

    def f(x, y):
        t = max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / n))
        return (x - x0 - t * dx) ** 2 + (y - y0 - t * dy) ** 2 <= (w / 2) ** 2
    return f


def tri(ax, ay, bx, by, cx, cy):
    def side(px, py, qx, qy, x, y):
        return (qx - px) * (y - py) - (qy - py) * (x - px)

    def f(x, y):
        s = (side(ax, ay, bx, by, x, y), side(bx, by, cx, cy, x, y), side(cx, cy, ax, ay, x, y))
        return all(v >= 0 for v in s) or all(v <= 0 for v in s)
    return f


def any_of(*shapes):
    return lambda x, y: any(s(x, y) for s in shapes)


def minus(a, b):
    return lambda x, y: a(x, y) and not b(x, y)


def path(points, w):
    return any_of(*[seg(*points[i], *points[i + 1], w) for i in range(len(points) - 1)])


def head(x, y, d, s=0.11):   # an arrowhead with its tip s past (x, y), pointing d: r l u d
    if d == "r":
        return tri(x, y - s, x, y + s, x + s * 1.3, y)
    if d == "l":
        return tri(x, y - s, x, y + s, x - s * 1.3, y)
    if d == "u":
        return tri(x - s, y, x + s, y, x, y - s * 1.3)
    return tri(x - s, y, x + s, y, x, y + s * 1.3)


W = 0.075   # stroke width


def bars(x0=0.20, x1=0.80, ys=(0.30, 0.50, 0.70)):
    return any_of(*[seg(x0, y, x1, y, W) for y in ys])


REPEAT = any_of(path([(0.20, 0.50), (0.20, 0.34), (0.66, 0.34)], W), head(0.66, 0.34, "r"),
                path([(0.80, 0.50), (0.80, 0.66), (0.34, 0.66)], W), head(0.34, 0.66, "l"))
NOTE = any_of(ellipse(0.36, 0.72, 0.13, 0.10), rect(0.43, 0.24, 0.49, 0.72),
              tri(0.43, 0.20, 0.43, 0.38, 0.72, 0.30), tri(0.43, 0.20, 0.72, 0.30, 0.72, 0.14))

ICONS = {
    "ic_play": tri(0.30, 0.24, 0.30, 0.76, 0.76, 0.50),
    "ic_pause": any_of(rect(0.28, 0.24, 0.43, 0.76), rect(0.57, 0.24, 0.72, 0.76)),
    "ic_stop": rect(0.27, 0.27, 0.73, 0.73),
    "ic_previous": any_of(rect(0.16, 0.28, 0.23, 0.72), tri(0.52, 0.28, 0.52, 0.72, 0.24, 0.50),
                          tri(0.82, 0.28, 0.82, 0.72, 0.52, 0.50)),
    "ic_next": any_of(rect(0.77, 0.28, 0.84, 0.72), tri(0.48, 0.28, 0.48, 0.72, 0.76, 0.50),
                      tri(0.18, 0.28, 0.18, 0.72, 0.48, 0.50)),
    "ic_shuffle": any_of(path([(0.14, 0.32), (0.30, 0.32), (0.58, 0.68), (0.72, 0.68)], W), head(0.72, 0.68, "r"),
                         path([(0.14, 0.68), (0.30, 0.68), (0.58, 0.32), (0.72, 0.32)], W), head(0.72, 0.32, "r")),
    "ic_repeat": REPEAT,
    "ic_repeat_one": any_of(REPEAT, path([(0.46, 0.45), (0.51, 0.42), (0.51, 0.58)], 0.055)),
    "ic_queue": any_of(bars(0.34, 0.82), circle(0.20, 0.30, 0.05), circle(0.20, 0.50, 0.05),
                       circle(0.20, 0.70, 0.05)),
    "ic_add_queue": any_of(bars(0.16, 0.60, (0.28, 0.46, 0.64)), seg(0.74, 0.50, 0.74, 0.78, W),
                           seg(0.60, 0.64, 0.88, 0.64, W)),
    "ic_play_next": any_of(bars(0.16, 0.52, (0.28, 0.46, 0.64)), tri(0.62, 0.44, 0.62, 0.80, 0.88, 0.62)),
    "ic_album": any_of(ring(0.50, 0.50, 0.33, W), circle(0.50, 0.50, 0.09)),
    "ic_info": any_of(ring(0.50, 0.50, 0.33, W), circle(0.50, 0.34, 0.05), seg(0.50, 0.46, 0.50, 0.66, W)),
    "ic_share": any_of(circle(0.28, 0.50, 0.10), circle(0.70, 0.26, 0.10), circle(0.70, 0.74, 0.10),
                       seg(0.28, 0.50, 0.70, 0.26, 0.05), seg(0.28, 0.50, 0.70, 0.74, 0.05)),
    "ic_radio": any_of(circle(0.50, 0.50, 0.08),
                       minus(any_of(ring(0.50, 0.50, 0.20, 0.06), ring(0.50, 0.50, 0.33, 0.06)),
                             lambda x, y: abs(x - 0.50) < abs(y - 0.50))),
    "ic_clear": any_of(seg(0.26, 0.26, 0.74, 0.74, W), seg(0.74, 0.26, 0.26, 0.74, W)),
    "ic_sort": any_of(seg(0.18, 0.30, 0.82, 0.30, W), seg(0.18, 0.50, 0.62, 0.50, W),
                      seg(0.18, 0.70, 0.42, 0.70, W)),
    "ic_reorder": any_of(seg(0.34, 0.34, 0.34, 0.80, W), head(0.34, 0.34, "u"),
                         seg(0.66, 0.20, 0.66, 0.66, W), head(0.66, 0.66, "d")),
    "ic_search": any_of(ring(0.43, 0.43, 0.22, W), seg(0.60, 0.60, 0.82, 0.82, 0.09)),
    "ic_note": NOTE,
}


def raster(size, inside):   # -> rows of coverage, 0..255
    rows = []
    for py in range(size):
        row = []
        for px in range(size):
            hit = sum(inside((px + (i + 0.5) / SS) / size, (py + (j + 0.5) / SS) / size)
                      for i in range(SS) for j in range(SS))
            row.append(hit * 255 // (SS * SS))
        rows.append(row)
    return rows


def write_png(path, size, pixels):   # pixels: rows of (r, g, b, a)
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    raw = b"".join(b"\x00" + b"".join(bytes(p) for p in row) for row in pixels)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, len(pixels), 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def icon(path, inside, size=81):
    write_png(path, size, [[(255, 255, 255, a) for a in row] for row in raster(size, inside)])


def placeholder(path, size):   # the stock player's: a dark note on a dark grey square, opaque
    bg, fg = 0x26, 0x00
    write_png(path, size, [[(bg + (fg - bg) * a // 255,) * 3 + (255,) for a in row] for row in raster(size, NOTE)])


out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "assets", "images")
os.makedirs(out, exist_ok=True)
for name, shape in ICONS.items():
    icon(os.path.join(out, name + ".png"), shape)
placeholder(os.path.join(out, "art_none.png"), 120)
placeholder(os.path.join(out, "art_none_large.png"), 360)

if len(sys.argv) > 1:   # a contact sheet: 6 per row on mid grey
    names, cell = sorted(ICONS), 81
    cols = 6
    grid = [[(70, 70, 70, 255)] * (cols * cell) for _ in range(((len(names) + cols - 1) // cols) * cell)]
    for n, name in enumerate(names):
        for y, row in enumerate(raster(cell, ICONS[name])):
            for x, a in enumerate(row):
                v = 70 + (255 - 70) * a // 255
                grid[(n // cols) * cell + y][(n % cols) * cell + x] = (v, v, v, 255)
    write_png(sys.argv[1], cols * cell, grid)
    print("sheet:", ", ".join(names))
