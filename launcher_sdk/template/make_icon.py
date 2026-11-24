#!/usr/bin/env python3
"""Generate hello.rgba for the template tile - stdlib only, no PIL.

Icon format the launcher reads (see evo_launcher.c load_icon):
    [u32 LE width][u32 LE height][width*height * u32 LE pixel]
    pixel = 0xAARRGGBB
A real app can draw a nicer icon (see projects/evo_launcher/tools/make_icons.py
for a PIL-based set); this one keeps zero dependencies.
"""
import struct, os

W = H = 140
BORDER = 6
def rgba(a, r, g, b): return (a << 24) | (r << 16) | (g << 8) | b

ACCENT = rgba(255, 0xE0, 0x80, 0x20)   # orange fill
EDGE   = rgba(255, 0x30, 0x30, 0x36)   # dark border
DOT    = rgba(255, 0xFF, 0xFF, 0xFF)   # white centre dot

buf = bytearray(struct.pack("<II", W, H))
cx, cy, rr = W // 2, H // 2, 24
for y in range(H):
    for x in range(W):
        if x < BORDER or y < BORDER or x >= W - BORDER or y >= H - BORDER:
            p = EDGE
        elif (x - cx) ** 2 + (y - cy) ** 2 < rr * rr:
            p = DOT
        else:
            p = ACCENT
        buf += struct.pack("<I", p)

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hello.rgba")
with open(out, "wb") as f:
    f.write(buf)
print("wrote %s (%dx%d)" % (out, W, H))
