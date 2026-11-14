#!/usr/bin/env python3
"""Draws the player's icons into app/assets/images: white on transparent, 81 x 81 px (9 du on a
Q10, the size of an action icon), with no library but zlib. Run again after changing a shape.

    python3 scripts/gen-icons.py
"""
import os, struct, zlib

SIZE, SS = 81, 4          # the icon's side, and samples per pixel along each axis


def inside_play(x, y):    # a triangle pointing right
    return 0.30 <= x <= 0.76 and abs(y - 0.5) <= (0.76 - x) * 0.56


def inside_pause(x, y):   # two bars
    return 0.24 <= y <= 0.76 and (0.28 <= x <= 0.43 or 0.57 <= x <= 0.72)


def png(path, inside):
    rows = []
    for py in range(SIZE):
        row = bytearray([0])   # filter: none
        for px in range(SIZE):
            hit = sum(inside((px + (i + 0.5) / SS) / SIZE, (py + (j + 0.5) / SS) / SIZE)
                      for i in range(SS) for j in range(SS))
            row += bytes((255, 255, 255, hit * 255 // (SS * SS)))
        rows.append(bytes(row))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))


out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "assets", "images")
os.makedirs(out, exist_ok=True)
png(os.path.join(out, "ic_play.png"), inside_play)
png(os.path.join(out, "ic_pause.png"), inside_pause)
